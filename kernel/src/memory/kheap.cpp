#include "kheap.h"
#include "../dbg/kasan.h"
#include "../kinit.h"
#include "utils/kdbg.h"
#include "utils/operations.h"
#include "vallocator.h"

void* operator new(std::size_t size)
{
     return memory::ExAllocatePool(memory::PoolType::NonPaged, size, memory::NonPagedPoolTag);
}
void* operator new[](std::size_t size)
{
     return memory::ExAllocatePool(memory::PoolType::NonPaged, size, memory::NonPagedPoolTag);
}
void operator delete(void* ptr) noexcept { memory::ExFreePool(ptr); }                                      // NOLINT
void operator delete(void* ptr, [[maybe_unused]] std::size_t size) noexcept { memory::ExFreePool(ptr); }   // NOLINT
void operator delete[](void* ptr) noexcept { memory::ExFreePool(ptr); }                                    // NOLINT
void operator delete[](void* ptr, [[maybe_unused]] std::size_t size) noexcept { memory::ExFreePool(ptr); } // NOLINT

using namespace memory;

static constexpr std::size_t AlignUp(std::size_t size, std::size_t align) { return (size + align - 1) & ~(align - 1); }

static std::size_t BinIndex(std::size_t size) noexcept
{
     if (size <= SMALL_MAX) return (size / SMALL_BIN_STEP) - 1;

     std::size_t threshold = LARGE_BIN_BASE * 2;
     for (std::size_t k = 0; k < NUM_LARGE_BINS; ++k, threshold *= 2)
          if (size <= threshold) return NUM_SMALL_BINS + k;

     return NUM_BINS - 1; // overflow
}

static constexpr std::size_t SlabSizeForBin(std::size_t binIdx, std::size_t actualSize = 0) noexcept
{
     constexpr std::size_t PAGE = 4096;
     constexpr std::size_t MAX_SLAB = 64uz * 1024uz * 1024uz;
     constexpr std::size_t TARGET_SLOTS = 64;

     std::size_t objSize{};
     if (binIdx < NUM_SMALL_BINS) objSize = (binIdx + 1) * SMALL_BIN_STEP;
     else if (binIdx < NUM_SMALL_BINS + NUM_LARGE_BINS)
          objSize = LARGE_BIN_BASE * (2uz << (binIdx - NUM_SMALL_BINS));
     else
          objSize = (actualSize > MAX_SLAB) ? actualSize : MAX_SLAB;

     const std::size_t oneObj = AlignUp(sizeof(PoolHeader) + objSize, PAGE);
     const std::size_t fourObj = AlignUp(TARGET_SLOTS * (sizeof(PoolHeader) + objSize), PAGE);
     return fourObj <= MAX_SLAB ? fourObj : oneObj;
}

static constexpr std::size_t NONPAGED_RESERVE_SIZE = 1uz * 1024uz * 1024uz * 1024uz; // 1 GiB VA
static constexpr std::size_t PAGED_RESERVE_SIZE = 4uz * 1024uz * 1024uz * 1024uz;    // 4 GiB VA

struct PoolReservation
{
     std::byte* base{};
     std::size_t reserved{};
     std::atomic_flag commitLock{};
     std::size_t committed{0};

     void* NO_ASAN Commit(std::size_t size) noexcept
     {
          constexpr std::size_t PAGE = 4096;
          size = AlignUp(size, PAGE);

          while (commitLock.test_and_set(std::memory_order::acquire)) operations::Yield();

          void* ret = nullptr;

          if (committed + size <= reserved)
          {
               std::byte* addr = base + committed;
               // debugging::DbgWrite(u8"[kheap] Committing pool memory at {} ({} bytes)\r\n", static_cast<void*>(addr),
               //                     size);

               void* result = g_kernelProcess->AllocateVirtualMemory(
                   addr, size, AllocationFlags::Commit | AllocationFlags::ImmediatePhysical,
                   MemoryProtection::ReadWrite);

               if (result)
               {
                    committed += size;
                    ret = addr;
               }
               else
                    debugging::DbgWrite(u8"[kheap] ERROR: VMM commit failed at {}\r\n", static_cast<void*>(addr));
          }
          else
          {
               debugging::DbgWrite(
                   u8"[kheap] ERROR: Pool reservation exhausted. Commit = {}, Committed = {}, Reserved = {}\r\n", size,
                   committed, reserved);
          }

          commitLock.clear(std::memory_order::release);

          return ret;
     }
};

static PoolReservation g_nonPagedReserve{};
static PoolReservation g_pagedReserve{};

struct alignas(64) PoolState
{
     BinFreeList bins[NUM_BINS];            // NOLINT
     PoolSegment* segments[NUM_BINS]{};     // NOLINT
     std::atomic_flag segLocks[NUM_BINS]{}; // NOLINT

     void LockSeg(std::size_t i) noexcept
     {
          while (segLocks[i].test_and_set(std::memory_order::acquire)) operations::Yield();
     }
     void UnlockSeg(std::size_t i) noexcept { segLocks[i].clear(std::memory_order::release); }
};

static PoolState g_nonPagedState{};
static PoolState g_pagedState{};

static PoolState& StateFor(PoolType t) noexcept { return t == PoolType::NonPaged ? g_nonPagedState : g_pagedState; }
static PoolReservation& ReserveFor(PoolType t) noexcept
{
     return t == PoolType::NonPaged ? g_nonPagedReserve : g_pagedReserve;
}

static PoolHeader* BumpCarve(PoolSegment* seg, std::size_t userSize) noexcept
{
     const std::size_t total = sizeof(PoolHeader) + userSize;
     const std::size_t segBytes = static_cast<std::size_t>(seg->endAddress - seg->startAddress);

     std::size_t offset = seg->bumpOffset.load(std::memory_order::relaxed);
     for (;;)
     {
          if (offset + total > segBytes) return nullptr;
          if (seg->bumpOffset.compare_exchange_weak(offset, offset + total, std::memory_order::relaxed,
                                                    std::memory_order::relaxed))
               break;
     }
     return reinterpret_cast<PoolHeader*>(seg->startAddress + offset);
}

static PoolSegment* GrowPool(PoolType type, std::size_t binIdx, std::size_t actualSize = 0) noexcept
{
     std::size_t slabSize = SlabSizeForBin(binIdx, actualSize);

     void* base = ReserveFor(type).Commit(slabSize);
     if (!base)
     {
          debugging::DbgWrite(u8"[kheap] ERROR: Failed to commit memory for new slab of bin {}!\r\n", binIdx);
          return nullptr;
     }

     auto* seg = new (base) PoolSegment{};
     std::size_t hdrEnd = AlignUp(sizeof(PoolSegment), ALIGNMENT);
     seg->startAddress = static_cast<std::byte*>(base) + hdrEnd;
     seg->endAddress = static_cast<std::byte*>(base) + slabSize;
     seg->bumpOffset.store(0, std::memory_order::relaxed);

     // debugging::DbgWrite(u8"[kheap] new slab bin={} type={} base={} size={}\r\n", binIdx,
     //                     (type == PoolType::NonPaged) ? u8"NP" : u8"PG", base, slabSize);

     PoolState& ps = StateFor(type);
     ps.LockSeg(binIdx);
     seg->lpNext = ps.segments[binIdx];
     ps.segments[binIdx] = seg;
     ps.UnlockSeg(binIdx);

     return seg;
}

void NO_ASAN memory::KiHeapInitialise()
{
     debugging::DbgWrite(u8"[kheap] Initialising kernel heaps...\r\n");

     g_nonPagedReserve.base = static_cast<std::byte*>(g_kernelProcess->AllocateVirtualMemory(
         nullptr, NONPAGED_RESERVE_SIZE, AllocationFlags::Reserve, MemoryProtection::ReadWrite));
     g_nonPagedReserve.reserved = NONPAGED_RESERVE_SIZE;

     g_pagedReserve.base = static_cast<std::byte*>(g_kernelProcess->AllocateVirtualMemory(
         nullptr, PAGED_RESERVE_SIZE, AllocationFlags::Reserve, MemoryProtection::ReadWrite));
     g_pagedReserve.reserved = PAGED_RESERVE_SIZE;

     debugging::DbgWrite(u8"[kheap] Reserved non-paged pool at {} ({} bytes), paged pool at {} ({} bytes)\r\n",
                         g_nonPagedReserve.base, g_nonPagedReserve.reserved, g_pagedReserve.base,
                         g_pagedReserve.reserved);

     debugging::DbgWrite(u8"[kheap] Heap init done. {} bins (demand-paged)\r\n", NUM_BINS);
}

void* NO_ASAN memory::ExAllocatePool(PoolType type, std::size_t size, std::uint32_t tag)
{
     if (!size)
     {
          debugging::DbgWrite(u8"[kheap] WARNING: Allocating zero bytes — using 1\r\n");
          size = 1;
     }

     const std::size_t alignedSize = AlignUp(size, ALIGNMENT);
     const std::size_t binIdx = BinIndex(alignedSize);
     PoolState& ps = StateFor(type);

     if (PoolHeader* hdr = ps.bins[binIdx].Pop())
     {
          hdr->isAllocated = true;
          hdr->tag = tag;
          void* ret = hdr + 1;
          // debugging::DbgWrite(u8"[kheap] alloc bin={} ptr={} (free-list)\r\n", binIdx, ret);
          KASANAllocateHeap(ret, alignedSize);
          return ret;
     }

     ps.LockSeg(binIdx);
     PoolSegment* seg = ps.segments[binIdx];
     ps.UnlockSeg(binIdx);

     for (PoolSegment* s = seg; s; s = s->lpNext)
     {
          if (PoolHeader* hdr = BumpCarve(s, alignedSize))
          {
               hdr->size = static_cast<std::uint32_t>(alignedSize);
               hdr->tag = tag;
               hdr->binIndex = static_cast<std::uint8_t>(binIdx);
               hdr->type = type;
               hdr->isAllocated = true;
               hdr->lpNextFree = nullptr;
               void* ret = hdr + 1;
               // debugging::DbgWrite(u8"[kheap] alloc bin={} ptr={} (bump)\r\n", binIdx, ret);
               KASANAllocateHeap(ret, alignedSize);
               return ret;
          }
     }

     PoolSegment* newSeg = GrowPool(type, binIdx, alignedSize);
     if (!newSeg)
     {
          debugging::DbgWrite(u8"[kheap] ERROR: Failed to grow pool for bin {}!\r\n", binIdx);
          return nullptr;
     }

     PoolHeader* hdr = BumpCarve(newSeg, alignedSize);
     if (!hdr)
     {
          debugging::DbgWrite(u8"[kheap] ERROR: Failed to carve new segment for bin {}!\r\n", binIdx);
          return nullptr;
     }

     hdr->size = static_cast<std::uint32_t>(alignedSize);
     hdr->tag = tag;
     hdr->binIndex = static_cast<std::uint8_t>(binIdx);
     hdr->type = type;
     hdr->isAllocated = true;
     hdr->lpNextFree = nullptr;

     void* ret = hdr + 1;
     // debugging::DbgWrite(u8"[kheap] alloc bin={} ptr={} (new-slab)\r\n", binIdx, ret);
     KASANAllocateHeap(ret, alignedSize);
     return ret;
}

void memory::ExFreePool(void* ptr)
{
     if (!ptr) return;

     auto* hdr = reinterpret_cast<PoolHeader*>(ptr) - 1;

     if (!hdr->isAllocated) [[unlikely]]
     {
          debugging::DbgWrite(u8"[kheap] DOUBLE-FREE at {}\r\n", ptr);
          return;
     }
     KASANFreeHeap(ptr, hdr->size);

     hdr->isAllocated = false;
     hdr->tag = 0;
     hdr->lpNextFree = nullptr;

     PoolState& ps = StateFor(hdr->type);
     ps.bins[hdr->binIndex].Push(hdr);

     // debugging::DbgWrite(u8"[kheap] free bin={} ptr={}\r\n", static_cast<std::size_t>(hdr->binIndex), ptr);
}

void memory::ExEnumerateSegments(PoolType type, void (*callback)(void* poolHandle, std::size_t size))
{
     PoolState& ps = StateFor(type);
     for (std::size_t i = 0; i < NUM_BINS; ++i)
     {
          ps.LockSeg(i);
          for (PoolSegment* seg = ps.segments[i]; seg; seg = seg->lpNext)
               callback(seg, static_cast<std::size_t>(seg->endAddress - seg->startAddress));
          ps.UnlockSeg(i);
     }
}

void memory::ExEnumeratePool(void* poolHandle, void (*callback)(PoolHeader* header))
{
     auto* seg = reinterpret_cast<PoolSegment*>(poolHandle);
     std::size_t carvedBytes = seg->bumpOffset.load(std::memory_order::acquire);
     std::byte* cursor = seg->startAddress;
     std::byte* end = seg->startAddress + carvedBytes;

     while (cursor + sizeof(PoolHeader) <= end)
     {
          auto* hdr = reinterpret_cast<PoolHeader*>(cursor);
          callback(hdr);
          cursor += sizeof(PoolHeader) + hdr->size;
     }
}
