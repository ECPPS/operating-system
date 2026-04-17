#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include "BootVideo.h"
#include "cpu/interrupts.h"
#include "cpu/mp.h"
#include "dbg/kasan.h"
#include "device/device.h"
#include "device/dpc.h"
#include "device/pcie.h"
#include "kinit.h"
#include "memory/kheap.h"
#include "memory/pallocator.h"
#include "memory/vallocator.h"
#include "object/object.h"
#include "process/process.h"
#include "process/taskScheduler.h"
#include "process/thread.h"
#include "sync/locks.h"
#include "utils/PE.h"
#include "utils/arch.h"
#include "utils/cpu.h"
#include "utils/identify.h"
#include "utils/kdbg.h"
#include "utils/memory.h"
#include "utils/operations.h"

#ifdef ARCH_ARM64
#ifdef COMPILER_MSVC
#include <intrin.h>
#endif
#endif
alignas(std::max_align_t) static std::byte g_kernelProcessStorage[sizeof(kernel::ProcessControlBlock)]; // NOLINT

struct DateTime
{
     std::uint32_t sec;
     std::uint32_t min;
     std::uint32_t hour;
     std::uint32_t day;
     std::uint32_t month;
     std::uint32_t year;
};

static bool IsLeap(std::uint32_t y) { return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0); }

static std::uint32_t DaysInMonth(std::uint32_t m, std::uint32_t y)
{
     static constexpr std::array<std::uint32_t, 12> days = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

     if (m == 2 && IsLeap(y)) return 29;

     return days[m - 1];
}

static DateTime UnixToDateTime(std::uint64_t t)
{
     DateTime dt{};

     dt.sec = t % 60;
     t /= 60;
     dt.min = t % 60;
     t /= 60;
     dt.hour = t % 24;
     t /= 24;

     std::uint32_t year = 1970;

     while (true)
     {
          const std::uint32_t daysYear = IsLeap(year) ? 366 : 365;
          if (t < daysYear) break;
          t -= daysYear;
          year++;
     }

     dt.year = year;

     std::uint32_t month = 1;
     while (true)
     {
          const std::uint32_t dim = DaysInMonth(month, year);
          if (t < dim) break;
          t -= dim;
          month++;
     }

     dt.month = month;
     dt.day = static_cast<std::uint32_t>(t + 1);

     return dt;
}

static void Format2(char* out, std::uint32_t v)
{
     out[0] = char('0' + (v / 10));
     out[1] = char('0' + (v % 10));
}

static void Format4(char* out, std::uint32_t v)
{
     out[0] = char('0' + ((v / 1000) % 10));
     out[1] = char('0' + ((v / 100) % 10));
     out[2] = char('0' + ((v / 10) % 10));
     out[3] = char('0' + (v % 10));
}

static const char* g_bootStatus = "Booting...";
static std::atomic<std::uint64_t> g_idleCounter{};

void KiIdleLoop()
{
     if (process::KeCurrentCpu()->isBSP)
     {
          while (true)
          {
               VidExchangeBuffers();

               operations::EnableInterrupts();
               operations::Yield();
               operations::Yield();
               operations::DisableInterrupts();

               device::KeFlushQueuedDpcs();

               operations::EnableInterrupts();
               operations::Halt();
          }
     }
     g_idleCounter.fetch_add(1, std::memory_order::acq_rel);

     while (true)
     {
          operations::EnableInterrupts();
          operations::Yield();
          operations::Yield();
          operations::DisableInterrupts();

          device::KeFlushQueuedDpcs();

          operations::EnableInterrupts();
          operations::Halt();
     }
}
void Error(std::uint32_t* buffer, arch::LoaderParameterBlock* param)
{
     for (std::size_t i = 0; i < param->framebuffer.height; i++)
          std::uninitialized_fill_n(buffer + (i * param->framebuffer.scanlineSize), param->framebuffer.scanlineSize,
                                    0x300af0);

     operations::DisableInterrupts();
     while (true) operations::Halt();
}

extern "C" void __asan_init();

static NO_ASAN void KiMarkBootVideoImage()
{
     auto* const videoBytes = reinterpret_cast<std::byte*>(g_loaderBlock->bootVideoVirtualBase);

     const auto* dos = reinterpret_cast<const DosHeader*>(videoBytes);
     const auto* nt = reinterpret_cast<const NtHeaders*>(videoBytes + dos->eLfanew);
     const auto& opt = nt->optionalHeader;

     {
          const std::size_t headerSize = (opt.sizeOfHeaders + 0xFFF) & ~0xFFFull;
          g_kernelProcess->ReserveMemoryFixedAsCommitted(g_loaderBlock->bootVideoVirtualBase, headerSize,
                                                         memory::MemoryProtection::ReadOnly,
                                                         memory::PFNUse::KernelHeap);
     }

     const std::uint16_t numSections = nt->fileHeader.numberOfSections;
     const auto* sections =
         reinterpret_cast<const SectionHeader*>(videoBytes + dos->eLfanew + sizeof(std::uint32_t) +
                                                sizeof(CoffFileHeader) + nt->fileHeader.sizeOfOptionalHeader);

     for (std::uint16_t i = 0; i < numSections; i++)
     {
          const SectionHeader& sec = sections[i];

          const std::uint32_t mapSize = sec.virtualSize > 0 ? sec.virtualSize : sec.sizeOfRawData;
          if (mapSize == 0) continue;

          const std::uintptr_t secVirt = g_loaderBlock->bootVideoVirtualBase + sec.virtualAddress;
          const std::size_t secSize = (mapSize + 0xFFF) & ~0xFFFull;

          const bool writable = (sec.characteristics & 0x80000000) != 0;
          const bool executable = (sec.characteristics & 0x20000000) != 0;

          memory::MemoryProtection prot{};
          if (executable && writable) prot = memory::MemoryProtection::ExecuteReadWrite;
          else if (executable)
               prot = memory::MemoryProtection::ExecuteRead;
          else if (writable)
               prot = memory::MemoryProtection::ReadWrite;
          else
               prot = memory::MemoryProtection::ReadOnly;

          g_kernelProcess->ReserveMemoryFixedAsCommitted(secVirt, secSize, prot, memory::PFNUse::KernelHeap);
     }
}

static NO_ASAN void KiMarkKernelImage()
{
     auto* const kernelBytes = reinterpret_cast<std::byte*>(g_loaderBlock->kernelVirtualBase);

     const auto* dos = reinterpret_cast<const DosHeader*>(kernelBytes);
     const auto* nt = reinterpret_cast<const NtHeaders*>(kernelBytes + dos->eLfanew);
     const auto& opt = nt->optionalHeader;

     {
          const std::size_t headerSize = (opt.sizeOfHeaders + 0xFFF) & ~0xFFFull;
          g_kernelProcess->ReserveMemoryFixedAsCommitted(g_loaderBlock->kernelVirtualBase, headerSize,
                                                         memory::MemoryProtection::ReadOnly,
                                                         memory::PFNUse::KernelHeap);
     }

     const std::uint16_t numSections = nt->fileHeader.numberOfSections;
     const auto* sections =
         reinterpret_cast<const SectionHeader*>(kernelBytes + dos->eLfanew + sizeof(std::uint32_t) +
                                                sizeof(CoffFileHeader) + nt->fileHeader.sizeOfOptionalHeader);

     for (std::uint16_t i = 0; i < numSections; i++)
     {
          const SectionHeader& sec = sections[i];

          const std::uint32_t mapSize = sec.virtualSize > 0 ? sec.virtualSize : sec.sizeOfRawData;
          if (mapSize == 0) continue;

          const std::uintptr_t secVirt = g_loaderBlock->kernelVirtualBase + sec.virtualAddress;
          const std::size_t secSize = (mapSize + 0xFFF) & ~0xFFFull;

          const bool writable = (sec.characteristics & 0x80000000) != 0;
          const bool executable = (sec.characteristics & 0x20000000) != 0;

          memory::MemoryProtection prot{};
          if (executable && writable) prot = memory::MemoryProtection::ExecuteReadWrite;
          else if (executable)
               prot = memory::MemoryProtection::ExecuteRead;
          else if (writable)
               prot = memory::MemoryProtection::ReadWrite;
          else
               prot = memory::MemoryProtection::ReadOnly;

          g_kernelProcess->ReserveMemoryFixedAsCommitted(secVirt, secSize, prot, memory::PFNUse::KernelHeap);
     }

     KiMarkBootVideoImage();
}

std::uintptr_t NO_ASAN KiInitialise(arch::LoaderParameterBlock* param)
{
     std::uint32_t* buffer = reinterpret_cast<std::uint32_t*>(param->framebuffer.physicalStart + 0xffff'8000'0000'0000);

     g_bootCycles = operations::ReadCurrentCycles();
     g_loaderBlock = param;
     auto framebuffer = param->framebuffer;

     g_stackBase = param->stackVirtualBase;
     g_stackSize = param->stackSize;
     g_imageBase = param->kernelVirtualBase;
     g_imageSize = param->kernelSize;

     std::uintptr_t mpPage = ~0;

     auto status = memory::physicalAllocator.Initialise(param->memoryDescriptors, param->kernelPhysicalBase,
                                                        0xffff'8000'0000'0000, param->kernelSize, mpPage);
     if (!status) Error(buffer, param);
     const auto gdtPage = memory::physicalAllocator.AllocatePage(memory::PFNUse::KernelHeap);
     const auto idtPage = memory::physicalAllocator.AllocatePage(memory::PFNUse::KernelHeap);

     std::uintptr_t pages[2] = {gdtPage, idtPage};
     cpu::Initialise(&pages);

     g_kernelProcess = new (g_kernelProcessStorage) kernel::ProcessControlBlock(0, "System", 0xffff'e000'0000'0000);
     g_kernelProcess->SetState(kernel::ProcessState::Running);
     g_kernelProcess->SetPriority(kernel::ProcessPriority::Realtime);
     g_kernelProcess->SetPageTableBase(memory::paging::GetCurrentPageTable());

     memory::virtualOffset = 0xffff'8000'0000'0000;

     g_kernelProcess->ReserveMemoryFixedAsCommitted(param->stackVirtualBase, param->stackSize,
                                                    memory::MemoryProtection::ReadWrite, memory::PFNUse::KernelStack);

     KiMarkKernelImage();

     g_kernelProcess->ReserveMemoryFixed(memory::virtualOffset, memory::physicalAllocator.databaseSize * 0x1000uz,
                                         memory::MemoryProtection::ReadWrite, memory::PFNUse::DriverLocked);

     KeInitialiseCpu(param->acpiPhysical);
     KeRemoveLeftoverMappings();
     cpu::KeProtect(
         [](std::uintptr_t address, bool isCode) -> void
         {
              memory::paging::ProtectPage(memory::paging::GetCurrentPageTable(), address, false, false, isCode,
                                          [](std::size_t) -> void*
                                          {
                                               std::uintptr_t page =
                                                   memory::physicalAllocator.AllocatePage(memory::PFNUse::PageTable);
                                               if (page == ~0uz) return nullptr;
                                               return reinterpret_cast<void*>(page + memory::virtualOffset);
                                          });
              memory::paging::InvalidatePage(address);
         });

     return mpPage;
}
void PrintVadEntryCallback(const memory::VADEntry& entry)
{
     using namespace debugging;

     DbgWrite(u8"Base: {}, Size: {}, Protection: ", reinterpret_cast<void*>(entry.baseAddress), entry.size);
     switch (entry.protection)
     {
     case memory::MemoryProtection::ReadOnly: DbgWrite(u8"RO"); break;
     case memory::MemoryProtection::ReadWrite: DbgWrite(u8"RW"); break;
     case memory::MemoryProtection::Execute: DbgWrite(u8"X"); break;
     case memory::MemoryProtection::ExecuteReadWrite: DbgWrite(u8"RWX"); break;
     default: DbgWrite(u8"?"); break;
     }

     DbgWrite(u8", State: ");
     switch (entry.state)
     {
     case memory::VADMemoryState::Free: DbgWrite(u8"Free"); break;
     case memory::VADMemoryState::Reserved: DbgWrite(u8"Reserved"); break;
     case memory::VADMemoryState::Committed: DbgWrite(u8"Committed"); break;
     case memory::VADMemoryState::PagedOut: DbgWrite(u8"PagedOut"); break;
     default: DbgWrite(u8"?"); break;
     }

     DbgWrite(u8", PFNUse: ");
     switch (entry.use)
     {
     case memory::PFNUse::ProcessPrivate: DbgWrite(u8"ProcessPrivate"); break;
     case memory::PFNUse::KernelHeap: DbgWrite(u8"KernelHeap"); break;
     case memory::PFNUse::UserStack: DbgWrite(u8"UserStack"); break;
     case memory::PFNUse::DriverLocked: DbgWrite(u8"DriverLocked"); break;
     case memory::PFNUse::FSCache: DbgWrite(u8"FSCache"); break;
     default: DbgWrite(u8"?"); break;
     }

     DbgWrite(u8"\r\n");
}
void PrintVMStats()
{
     const auto& stats = g_kernelProcess->GetVadAllocator().GetStatistics();

     debugging::DbgWrite(u8"==== Virtual Memory Statistics ====\r\n");
     debugging::DbgWrite(u8"Total Reserved: ");
     debugging::SerialFormatter<std::size_t>::Write(stats.totalReservedBytes.load());
     debugging::DbgWrite(u8"\r\nTotal Committed: ");
     debugging::SerialFormatter<std::size_t>::Write(stats.totalCommittedBytes.load());
     debugging::DbgWrite(u8"\r\nTotal Immediate: ");
     debugging::SerialFormatter<std::size_t>::Write(stats.totalImmediateBytes.load());
     debugging::DbgWrite(u8"\r\nPeak Reserved: ");
     debugging::SerialFormatter<std::size_t>::Write(stats.peakReservedBytes.load());
     debugging::DbgWrite(u8"\r\nPeak Committed: ");
     debugging::SerialFormatter<std::size_t>::Write(stats.peakCommittedBytes.load());
     debugging::DbgWrite(u8"\r\nCommit Charge: ");
     debugging::SerialFormatter<std::size_t>::Write(stats.commitCharge.load());
     debugging::DbgWrite(u8"\r\nReserve Ops: ");
     debugging::SerialFormatter<std::size_t>::Write(stats.reserveOperations.load());
     debugging::DbgWrite(u8"\r\nCommit Ops: ");
     debugging::SerialFormatter<std::size_t>::Write(stats.commitOperations.load());
     debugging::DbgWrite(u8"\r\nDecommit Ops: ");
     debugging::SerialFormatter<std::size_t>::Write(stats.decommitOperations.load());
     debugging::DbgWrite(u8"\r\nRelease Ops: ");
     debugging::SerialFormatter<std::size_t>::Write(stats.releaseOperations.load());
     debugging::DbgWrite(u8"\r\nFailed Allocations: ");
     debugging::SerialFormatter<std::size_t>::Write(stats.failedAllocations.load());
     debugging::DbgWrite(u8"\r\n==== End of VM Stats ====\r\n");
}
void PrintPhysicalStats()
{
     auto& pma = memory::physicalAllocator;

     debugging::DbgWrite(u8"==== Physical Memory Statistics ====\r\n");
     debugging::DbgWrite(u8"Active Pages: ");
     debugging::SerialFormatter<std::size_t>::Write(pma.activePages.load());
     debugging::DbgWrite(u8"\r\nFree Pages: ");
     debugging::SerialFormatter<std::size_t>::Write(pma.freePages.load());
     debugging::DbgWrite(u8"\r\nZero Pages: ");
     debugging::SerialFormatter<std::size_t>::Write(pma.zeroPages.load());
     debugging::DbgWrite(u8"\r\nStandby Pages: ");
     debugging::SerialFormatter<std::size_t>::Write(pma.standbyPages.load());
     debugging::DbgWrite(u8"\r\nModified Pages: ");
     debugging::SerialFormatter<std::size_t>::Write(pma.modifiedPages.load());
     debugging::DbgWrite(u8"\r\nBad Pages: ");
     debugging::SerialFormatter<std::size_t>::Write(pma.badPages.load());

     debugging::DbgWrite(u8"\r\nUnused Pages: ");
     debugging::SerialFormatter<std::size_t>::Write(pma.unusedPages.load());
     debugging::DbgWrite(u8"\r\nProcessPrivate Pages: ");
     debugging::SerialFormatter<std::size_t>::Write(pma.processPrivatePages.load());
     debugging::DbgWrite(u8"\r\nMappedFile Pages: ");
     debugging::SerialFormatter<std::size_t>::Write(pma.mappedFilePages.load());
     debugging::DbgWrite(u8"\r\nDriverLocked Pages: ");
     debugging::SerialFormatter<std::size_t>::Write(pma.driverLockedPages.load());
     debugging::DbgWrite(u8"\r\nUserStack Pages: ");
     debugging::SerialFormatter<std::size_t>::Write(pma.userStackPages.load());
     debugging::DbgWrite(u8"\r\nKernelStack Pages: ");
     debugging::SerialFormatter<std::size_t>::Write(pma.kernelStackPages.load());
     debugging::DbgWrite(u8"\r\nKernelHeap Pages: ");
     debugging::SerialFormatter<std::size_t>::Write(pma.kernelHeapPages.load());
     debugging::DbgWrite(u8"\r\nMetaFile Pages: ");
     debugging::SerialFormatter<std::size_t>::Write(pma.metaFilePages.load());
     debugging::DbgWrite(u8"\r\nNonPagedPool Pages: ");
     debugging::SerialFormatter<std::size_t>::Write(pma.nonPagedPoolPages.load());
     debugging::DbgWrite(u8"\r\nPagedPool Pages: ");
     debugging::SerialFormatter<std::size_t>::Write(pma.pagedPoolPages.load());
     debugging::DbgWrite(u8"\r\nPTE Pages: ");
     debugging::SerialFormatter<std::size_t>::Write(pma.ptePages.load());
     debugging::DbgWrite(u8"\r\nShareable Pages: ");
     debugging::SerialFormatter<std::size_t>::Write(pma.shareablePages.load());
     debugging::DbgWrite(u8"\r\nPageTable Pages: ");
     debugging::SerialFormatter<std::size_t>::Write(pma.pageTablePages.load());
     debugging::DbgWrite(u8"\r\nFSCache Pages: ");
     debugging::SerialFormatter<std::size_t>::Write(pma.fsCachePages.load());
     debugging::DbgWrite(u8"\r\n==== End of Physical Stats ====\r\n");
}
static std::size_t PrintSize(std::size_t bytes)
{
     constexpr std::size_t KiB = 1024;
     constexpr std::size_t MiB = 1024 * KiB;
     constexpr std::size_t GiB = 1024 * MiB;

     std::size_t written{};

     auto print = [&](std::size_t v, const char8_t* s)
     {
          debugging::DbgWrite(u8"{} {}", v, s);

          std::array<char, 32> buffer{};
          auto [p, ec] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), v);
          written = static_cast<std::size_t>(p - buffer.data()) + 1;

          while (*s++) ++written;
     };

     if (bytes >= GiB) print(bytes / GiB, u8"GiB");
     else if (bytes >= MiB)
          print(bytes / MiB, u8"MiB");
     else if (bytes >= KiB)
          print(bytes / KiB, u8"KiB");
     else
          print(bytes, u8"B");

     return written;
}
static void Pad(std::size_t count)
{
     for (std::size_t i = 0; i < count; i++) debugging::DbgWrite(u8" ");
}
static void PrintCol(const char8_t* s, std::size_t width)
{
     std::size_t len{};

     while (s[len]) ++len;

     debugging::DbgWrite(u8"{}", s);

     if (len < width) Pad(width - len);
}
static void PrintVADHeader()
{
     debugging::DbgWrite(u8"\r\n");

     PrintCol(u8"Base", 18);
     PrintCol(u8"End", 18);
     PrintCol(u8"Size", 10);
     PrintCol(u8"State", 12);
     PrintCol(u8"Protect", 20);
     PrintCol(u8"Type", 16);

     debugging::DbgWrite(u8"\r\n");
}
static std::size_t PrintHex(std::uintptr_t v)
{
     std::array<char, 17> buffer{};
     auto [ptr, ec] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), v, 16);
     *ptr = '\0';

     debugging::DbgWrite(u8"{}", reinterpret_cast<const char8_t*>(buffer.data()));
     return ptr - buffer.data();
}
static const char8_t* ProtToStr(memory::MemoryProtection p)
{
     using enum memory::MemoryProtection;

     switch (p)
     {
     case memory::MemoryProtection::ReadOnly: return u8"read-only";
     case memory::MemoryProtection::ReadWrite: return u8"read-write";
     case memory::MemoryProtection::Execute: return u8"execute-only";
     case memory::MemoryProtection::ExecuteRead: return u8"read-execute";
     case memory::MemoryProtection::ExecuteReadWrite: return u8"read-execute-write";
     default: return u8"Unknown";
     }
}
static const char8_t* StateToStr(memory::VADMemoryState s)
{
     using enum memory::VADMemoryState;

     switch (s)
     {
     case Free: return u8"Free";
     case Reserved: return u8"Reserved";
     case Committed: return u8"Committed";
     default: return u8"Unknown";
     }
}
static const char8_t* UseToStr(memory::PFNUse u)
{
     using enum memory::PFNUse;

     switch (u)
     {
     case ProcessPrivate: return u8"ProcessPrivate";
     case MappedFile: return u8"MappedFile";
     case DriverLocked: return u8"DriverLocked";
     case UserStack: return u8"UserStack";
     case KernelStack: return u8"KernelStack";
     case KernelHeap: return u8"KernelHeap";
     case NonPagedPool: return u8"NonPagedPool";
     case PagedPool: return u8"PagedPool";
     case PageTable: return u8"PageTable";
     case FSCache: return u8"FSCache";
     default: return u8"Other";
     }
}
static void PrintVADRow(const memory::VADEntry& e)
{
     const auto base = e.baseAddress;
     const auto end = e.baseAddress + e.size;

     PrintHex(base);
     Pad(18 - 16);

     PrintHex(end);
     Pad(18 - 16);

     const auto sizeWidth = PrintSize(e.size);
     Pad(10 - sizeWidth);

     PrintCol(StateToStr(e.state), 12);
     PrintCol(ProtToStr(e.protection), 20);
     PrintCol(UseToStr(e.use), 16);

     debugging::DbgWrite(u8"\r\n");
}
constexpr const char8_t* FormatSize(std::size_t size, std::size_t& outVal)
{
     if (size >= 1024uz * 1024uz * 1024)
     {
          outVal = size / (1024uz * 1024 * 1024);
          return u8"GiB";
     }
     if (size >= 1024uz * 1024)
     {
          outVal = size / (1024uz * 1024);
          return u8"MiB";
     }
     if (size >= 1024)
     {
          outVal = size / 1024;
          return u8"KiB";
     }
     outVal = size;
     return u8"B";
}

static std::size_t AlignUp(std::size_t size, std::size_t align) { return (size + align - 1) & ~(align - 1); }

#ifdef ARCH_X8664
constexpr std::uint16_t COM1_PORT = 0x3F8;

#ifdef COMPILER_MSVC // MSVC vvv
inline void Out8(std::uint16_t port, std::uint8_t value) { __outbyte(port, value); }

inline std::uint8_t In8(std::uint16_t port) { return static_cast<std::uint8_t>(__inbyte(port)); }
#elifdef COMPILER_CLANG // ^^^ MSVC / Clang vvv
inline void Out8(std::uint16_t port, std::uint8_t value) { asm volatile("outb %0, %1" : : "a"(value), "Nd"(port)); }

inline std::uint8_t In8(std::uint16_t port)
{
     std::uint8_t value{};
     asm volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
     return value;
}
#endif                  // ^^^ Clang

bool SerialInterruptHandler([[maybe_unused]] cpu::IInterruptFrame& frame, void* unused)
{
     const std::uint8_t iir = In8(COM1_PORT + 2);
     if (iir & 0x01) return false;

     switch ((iir >> 1) & 0x3)
     {
     case 0x2:
     {
          while (In8(COM1_PORT + 5) & 0x01) operations::SerialPushCharacter(static_cast<char>(In8(COM1_PORT)));
          break;
     }

     case 0x1:
     {
          operations::SerialHoldLineHigh();
          break;
     }
     default: break;
     }

     return true;
}
#endif

static constexpr std::uint32_t kColBackground = 0;
static constexpr std::uint32_t kColDefault = 0xd4d4d4;
static constexpr std::uint32_t kColTreeLine = 0x4a4a6a;
static constexpr std::uint32_t kColBracket = 0x608060;    // [TypeName]
static constexpr std::uint32_t kColTypeName = 0x4ec994;   // the type string itself
static constexpr std::uint32_t kColObjectName = 0xe0c070; // "name"
static constexpr std::uint32_t kColSymlink = 0x7ab4f5;    // -> target
static constexpr std::uint32_t kColNumber = 0xce9178;     // numeric values
static constexpr std::uint32_t kColKey = 0x9cdcfe;        // "refs=", "CPU", etc.
static constexpr std::uint32_t kColBsp = 0xf44747;        // BSP label
static constexpr std::uint32_t kColAp = 0x608060;         // AP label
static constexpr std::uint32_t kColEmpty = 0x666680;      // (empty)
static constexpr std::uint32_t kColDepthLimit = 0xf44747; // depth warning

struct VidTextCtx
{
     std::uint32_t x{};
     std::uint32_t y{};
     std::uint32_t screenW{};
     std::uint32_t screenH{};
     std::uint8_t scale{1};
     std::uint32_t charW{};
     std::uint32_t charH{};
     std::uint32_t marginLeft{};
};
static void VtPutChar(VidTextCtx& ctx, char c, std::uint32_t colour) noexcept
{
     if (ctx.y + ctx.charH > ctx.screenH) return;

     if (c == '\n')
     {
          ctx.x = ctx.marginLeft;
          ctx.y += ctx.charH;
          return;
     }

     if (ctx.x + ctx.charW > ctx.screenW)
     {
          ctx.x = ctx.marginLeft;
          ctx.y += ctx.charH;
          if (ctx.y + ctx.charH > ctx.screenH) return;
     }

     VidDrawChar(ctx.x, ctx.y, c, colour, ctx.scale);
     ctx.x += ctx.charW;
}

static void VtPutStr(VidTextCtx& ctx, const char* s, std::uint32_t colour) noexcept
{
     if (s == nullptr) return;
     while (*s) VtPutChar(ctx, *s++, colour);
}

static void VtPutStr8(VidTextCtx& ctx, const char8_t* s, std::uint32_t colour) noexcept
{ VtPutStr(ctx, reinterpret_cast<const char*>(s), colour); }

static void VtPutU64(VidTextCtx& ctx, std::uint64_t v, std::uint32_t colour) noexcept
{
     char buf[21]; // NOLINT
     std::size_t len = 0;
     if (v == 0) { buf[len++] = '0'; }
     else
     {
          while (v > 0)
          {
               buf[len++] = static_cast<char>('0' + (v % 10));
               v /= 10;
          }

          for (std::size_t i = 0, j = len - 1; i < j; ++i, --j)
          {
               const char tmp = buf[i];
               buf[i] = buf[j];
               buf[j] = tmp;
          }
     }
     buf[len] = '\0';
     VtPutStr(ctx, buf, colour);
}

static void VtPutU32(VidTextCtx& ctx, std::uint32_t v, std::uint32_t colour) noexcept { VtPutU64(ctx, v, colour); }

static void VtPutU16(VidTextCtx& ctx, std::uint16_t v, std::uint32_t colour) noexcept { VtPutU64(ctx, v, colour); }

static void VtPutHex8(VidTextCtx& ctx, std::uint8_t v, std::uint32_t colour) noexcept
{
     constexpr const char kHex[] = "0123456789abcdef";
     char buf[3] = {kHex[v >> 4], kHex[v & 0xF], '\0'};
     VtPutStr(ctx, buf, colour);
}

static void VtPutHex16(VidTextCtx& ctx, std::uint16_t v, std::uint32_t colour) noexcept
{
     constexpr const char kHex[] = "0123456789abcdef";
     char buf[5] = {kHex[(v >> 12) & 0xF], kHex[(v >> 8) & 0xF], kHex[(v >> 4) & 0xF], kHex[v & 0xF], '\0'};
     VtPutStr(ctx, buf, colour);
}

[[nodiscard]] static const char* ObjTypeName(object::ObjectType t) noexcept
{
     switch (t)
     {
     case object::ObjectType::Process: return "Process";
     case object::ObjectType::Thread: return "Thread";
     case object::ObjectType::File: return "File";
     case object::ObjectType::Event: return "Event";
     case object::ObjectType::Mutex: return "Mutex";
     case object::ObjectType::Semaphore: return "Semaphore";
     case object::ObjectType::Timer: return "Timer";
     case object::ObjectType::Section: return "Section";
     case object::ObjectType::Port: return "Port";
     case object::ObjectType::SymbolicLink: return "SymLink";
     case object::ObjectType::Directory: return "Directory";
     case object::ObjectType::Device: return "Device";
     case object::ObjectType::Driver: return "Driver";
     case object::ObjectType::TypeDescriptor: return "TypeDesc";
     case object::ObjectType::Processor: return "Processor";
     default: return "Unknown";
     }
}

static void VtPutIndentAndBranch(VidTextCtx& ctx, const char* indent, bool isLast) noexcept
{
     VtPutStr(ctx, indent, kColTreeLine);
     VtPutStr(ctx, isLast ? "`-- " : "|-- ", kColTreeLine);
}

static void VtEmitSymlinkAnnotation(VidTextCtx& ctx, const object::ObjectHeader* node) noexcept
{
     const auto* sl = node->BodyAs<object::SymbolicLinkBody>();
     VtPutStr(ctx, " -> ", kColTreeLine);
     VtPutChar(ctx, '"', kColObjectName);
     VtPutStr(ctx, reinterpret_cast<const char*>(sl->targetPath.data()), kColSymlink);
     VtPutChar(ctx, '"', kColObjectName);
}

static void VtEmitTypeDescAnnotation(VidTextCtx& ctx, const object::ObjectHeader* node) noexcept
{
     const auto* td = node->BodyAs<object::TypeDescriptorBody>();
     VtPutStr(ctx, " (typeId=", kColKey);
     VtPutU16(ctx, static_cast<std::uint16_t>(td->typeId), kColNumber);
     VtPutChar(ctx, ')', kColKey);
}

static void VtEmitProcessorAnnotation(VidTextCtx& ctx, const object::ObjectHeader* node) noexcept
{
     const auto* pr = node->BodyAs<object::ProcessorBody>();
     VtPutStr(ctx, " (CPU", kColKey);
     VtPutU32(ctx, pr->logicalIndex, kColNumber);
     VtPutStr(ctx, " APIC=", kColKey);
     VtPutU32(ctx, pr->apicId, kColNumber);
     VtPutChar(ctx, ' ', kColKey);
     VtPutStr(ctx, pr->isBsp ? "BSP" : "AP", pr->isBsp ? kColBsp : kColAp);
     VtPutChar(ctx, ')', kColKey);
}

static void VtEmitPciChain(VidTextCtx& ctx, const device::Device* dev) noexcept
{
     if (dev->type != device::DeviceType::PCI)
     {
          VtPutStr(ctx, " (Device)", kColKey);
          return;
     }

     const auto* pci = static_cast<const PCIDevice*>(dev);

     if (pci->parent)
     {
          VtPutStr(ctx, " PCI ", kColKey);
          VtPutHex16(ctx, pci->segment, kColNumber);
          VtPutChar(ctx, ':', kColTreeLine);
          VtPutHex8(ctx, pci->bus, kColNumber);
          VtPutChar(ctx, ':', kColTreeLine);
          VtPutHex8(ctx, pci->device, kColNumber);
          VtPutChar(ctx, '.', kColTreeLine);
          VtPutHex8(ctx, pci->function, kColNumber);
          VtPutStr(ctx, " parent =", kColKey);
          VtEmitPciChain(ctx, pci->parent);
     }
     else
     {
          VtPutStr(ctx, " PCI ", kColKey);
          VtPutHex16(ctx, pci->segment, kColNumber);
          VtPutChar(ctx, ':', kColTreeLine);
          VtPutHex8(ctx, pci->bus, kColNumber);
          VtPutChar(ctx, ':', kColTreeLine);
          VtPutHex8(ctx, pci->device, kColNumber);
          VtPutChar(ctx, '.', kColTreeLine);
          VtPutHex8(ctx, pci->function, kColNumber);
     }
}

static void VtEmitDeviceAnnotation(VidTextCtx& ctx, const object::ObjectHeader* node) noexcept
{
     const auto* dev = node->BodyAs<device::Device>();
     VtEmitPciChain(ctx, dev);
}

static constexpr std::size_t kMaxChildren = 256;
static constexpr std::uint32_t kMaxDepth = 16;

static void VtPrintNode(VidTextCtx& ctx, const object::ObjectHeader* node,
                        char* indent, // mutable, max 64 bytes
                        std::size_t& indentLen, std::uint32_t depth) noexcept
{
     if (ctx.y + ctx.charH > ctx.screenH) return; // no screen space left

     if (depth > kMaxDepth)
     {
          VtPutStr(ctx, indent, kColTreeLine);
          VtPutStr(ctx, "... (depth limit)\n", kColDepthLimit);
          return;
     }

     if (node->type != object::ObjectType::Directory) return;

     struct SnapCtx
     {
          const object::ObjectHeader** children;
          std::size_t* count;
     };

     const object::ObjectHeader* children[kMaxChildren]{};
     std::size_t childCount = 0;
     SnapCtx snapCtx{.children = children, .count = &childCount};

     node->BodyAs<object::DirectoryBody>()->Enumerate(
         [](const object::ObjectHeader* child, void* raw) noexcept
         {
              auto* s = static_cast<SnapCtx*>(raw);
              if (*s->count < kMaxChildren) s->children[(*s->count)++] = child;
         },
         &snapCtx);

     if (childCount == 0)
     {
          VtPutStr(ctx, indent, kColTreeLine);
          VtPutStr(ctx, "(empty)\n", kColEmpty);
          return;
     }

     for (std::size_t i = 0; i < childCount; ++i)
     {
          if (ctx.y + ctx.charH > ctx.screenH) return;

          const object::ObjectHeader* child = children[i];
          const bool last = (i == childCount - 1);

          VtPutIndentAndBranch(ctx, indent, last);

          VtPutChar(ctx, '[', kColBracket);
          VtPutStr(ctx, ObjTypeName(child->type), kColTypeName);
          VtPutChar(ctx, ']', kColBracket);
          VtPutChar(ctx, ' ', kColDefault);

          VtPutChar(ctx, '"', kColDefault);
          {
               const auto view = child->accessName.View();
               for (char ci : view) VtPutChar(ctx, static_cast<char>(ci), kColObjectName);
          }
          VtPutChar(ctx, '"', kColDefault);

          switch (child->type)
          {
          case object::ObjectType::SymbolicLink: VtEmitSymlinkAnnotation(ctx, child); break;
          case object::ObjectType::TypeDescriptor: VtEmitTypeDescAnnotation(ctx, child); break;
          case object::ObjectType::Processor: VtEmitProcessorAnnotation(ctx, child); break;
          case object::ObjectType::Device: VtEmitDeviceAnnotation(ctx, child); break;
          default: break;
          }

          VtPutStr(ctx, "  refs=", kColKey);
          VtPutU64(ctx, child->referenceCount.load(std::memory_order_relaxed), kColNumber);
          VtPutChar(ctx, '\n', kColDefault);

          if (child->type == object::ObjectType::Directory)
          {
               const char* ext = last ? "    " : "|   ";
               std::size_t extLen = 0;
               while (ext[extLen]) ++extLen;

               if (indentLen + extLen + 1 < 64)
               {
                    std::memcpy(indent + indentLen, ext, extLen);
                    indentLen += extLen;
                    indent[indentLen] = '\0';

                    VtPrintNode(ctx, child, indent, indentLen, depth + 1);

                    indentLen -= extLen;
                    indent[indentLen] = '\0';
               }
          }
     }
}

void VidDumpObjectTree(const object::ObjectHeader* root) noexcept
{
     if (root == nullptr) return;

     VidClearScreen(kColBackground);

     VidTextCtx ctx{};
     VidGetDimensions(ctx.screenW, ctx.screenH);
     ctx.scale = 1;
     ctx.charW = 8 * ctx.scale;
     ctx.charH = 16 * ctx.scale;
     ctx.marginLeft = ctx.x;

     ctx.x = ctx.marginLeft;
     ctx.y = ctx.charH + 18;

     char indent[64]{};
     std::size_t indentLen = 0;
     VtPrintNode(ctx, root, indent, indentLen, 0);

     // Flush to screen.
     VidExchangeBuffers();
}

// PCIe, MP,
static constexpr std::size_t initialisationSteps = 6;
static std::size_t currentStep = 0;
static std::array<const char*, initialisationSteps> initStepNames = {"CPU", "PCIe", "MP", "MP lock", "Serial", "Done"};

static void RedrawBar()
{
     std::uint32_t screenWidth{};
     std::uint32_t screenHeight{};
     VidGetDimensions(screenWidth, screenHeight);

     constexpr std::uint32_t colEmpty = 0x666680;
     constexpr std::uint32_t colFilled = 0x4ec994;

     const std::uint32_t barX = screenWidth / 4;
     const std::uint32_t barY = screenHeight * 3 / 4;
     const std::uint32_t barWidth = screenWidth / 2;
     const std::uint32_t barHeight = 20;
     const std::uint32_t stepWidth = barWidth / initialisationSteps;

     for (std::uint32_t i = 0; i < initialisationSteps; i++)
     {
          const std::uint32_t x = barX + (i * stepWidth);
          const std::uint32_t colour = (i < currentStep) ? colFilled : colEmpty;
          VidDrawRect(x, barY, stepWidth - 2, barHeight, colour);
     }

     if (currentStep <= initialisationSteps)
     {
          constexpr auto maxStepNameLength = 16;
          const char* stepName = initStepNames[currentStep - 1];
          if (stepName == nullptr) stepName = "Unknown Step";
          std::uint32_t textWidth = 16 * static_cast<std::uint32_t>(std::strlen(stepName));
          std::uint32_t textX = barX + ((barWidth - textWidth) / 2);
          std::uint32_t textY = barY - 30;
          VidDrawRect(barX + ((barWidth - (maxStepNameLength * 16)) / 2) - 4, textY - 4, maxStepNameLength * 16, 24,
                      kColBackground);
          for (std::size_t i = 0; stepName[i]; i++) VidDrawChar(textX + (i * 16), textY, stepName[i], colFilled, 2);
     }

     VidExchangeBuffers();
}
static void UpdateProgressBar()
{
     currentStep++;
     RedrawBar();
}

// ReadMsr, WriteMsr, ReadCR, WriteCR
static inline std::uint64_t ReadMsr(std::uint32_t msr)
{
     std::uint32_t low{};
     std::uint32_t high{};
#ifdef COMPILER_MSVC
     low = __readmsr(msr);
     high = __readmsr(msr + 1);
#elif defined(COMPILER_CLANG)
     asm volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
#endif
     return (static_cast<std::uint64_t>(high) << 32) | low;
}
static inline void WriteMsr(std::uint32_t msr, std::uint64_t value)
{
     std::uint32_t low = static_cast<std::uint32_t>(value & 0xFFFFFFFF);
     std::uint32_t high = static_cast<std::uint32_t>(value >> 32);
#ifdef COMPILER_MSVC
     __writemsr(msr, low);
     __writemsr(msr + 1, high);
#elif defined(COMPILER_CLANG)
     asm volatile("wrmsr" : : "c"(msr), "a"(low), "d"(high));
#endif
}
static inline std::uint64_t ReadCR(std::uint32_t cr)
{
     std::uint64_t value{};
#ifdef COMPILER_MSVC
     switch (cr)
     {
     case 0: value = __readcr0(); break;
     case 4: value = __readcr4(); break;
     default: break;
     }
#elif defined(COMPILER_CLANG)
     switch (cr)
     {
     case 0: asm volatile("mov %%cr0, %0" : "=r"(value)); break;
     case 4: asm volatile("mov %%cr4, %0" : "=r"(value)); break;
     default: break;
     }
#endif
     return value;
}
static inline void WriteCR(std::uint32_t cr, std::uint64_t value)
{
#ifdef COMPILER_MSVC
     switch (cr)
     {
     case 0: __writecr0(value); break;
     case 4: __writecr4(value); break;
     default: break;
     }
#elif defined(COMPILER_CLANG)
     switch (cr)
     {
     case 0: asm volatile("mov %0, %%cr0" : : "r"(value)); break;
     case 4: asm volatile("mov %0, %%cr4" : : "r"(value)); break;
     default: break;
     }
#endif
}

static void KeEnableVMXOnCurrentProcessor()
{
     // IA32_VMX_PROC_CTLS: pin-based VM-execution controls
     // IA32_VMX_PROC_CTLS2: secondary processor-based VM-execution controls (if allowed by IA32_VMX_PROC_CTLS)
     // IA32_VMX_ENTRY_CTLS: VM-entry controls
     // IA32_VMX_EXIT_CTLS: VM-exit controls
     // IA32_VMX_MISC: various VMX capabilities

     constexpr std::uint64_t IA32_VMX_PROC_CTLS = 0x482;
     constexpr std::uint64_t IA32_VMX_PROC_CTLS2 = 0x48B;
     constexpr std::uint64_t IA32_VMX_ENTRY_CTLS = 0x484;
     constexpr std::uint64_t IA32_VMX_EXIT_CTLS = 0x483;
     constexpr std::uint64_t IA32_VMX_MISC = 0x485;

     const auto procCtls = ReadMsr(IA32_VMX_PROC_CTLS);
     const auto procCtls2 = ReadMsr(IA32_VMX_PROC_CTLS2);
     const auto entryCtls = ReadMsr(IA32_VMX_ENTRY_CTLS);
     const auto exitCtls = ReadMsr(IA32_VMX_EXIT_CTLS);
     const auto misc = ReadMsr(IA32_VMX_MISC);

     if ((procCtls & (1ull << 2)) == 0 || (procCtls & (1ull << 9)) == 0 || (entryCtls & (1ull << 9)) == 0 ||
         (exitCtls & (1ull << 15)) == 0)
     {
          debugging::DbgWrite(u8"Required VMX controls not supported on this CPU. Halting.\r\n");
          debugging::DbgWrite(u8"IA32_VMX_PROC_CTLS: {:x}\r\n", procCtls);
          debugging::DbgWrite(u8"IA32_VMX_PROC_CTLS2: {:x}\r\n", procCtls2);
          debugging::DbgWrite(u8"IA32_VMX_ENTRY_CTLS: {:x}\r\n", entryCtls);
          debugging::DbgWrite(u8"IA32_VMX_EXIT_CTLS: {:x}\r\n", exitCtls);
          debugging::DbgWrite(u8"IA32_VMX_MISC: {:x}\r\n", misc);
          process::KeExitCurrentThread();
     }

     std::uint64_t vmxProcCtls = procCtls | (1ull << 2) | (1ull << 9);
     if (procCtls2 & (1ull << 0)) vmxProcCtls |= (1ull << 31);
     WriteMsr(IA32_VMX_PROC_CTLS, vmxProcCtls);
     WriteCR(4, ReadCR(4) | (1ull << 13));

     debugging::DbgWrite(u8"VMX enabled on current processor\r\n");
}

extern "C" NO_ASAN int KiStartup(arch::LoaderParameterBlock* param)
{
     debugging::DbgWrite(u8"Kernel image base: {:x}, size: {:x}\r\n", param->kernelVirtualBase, param->kernelSize);
     __security_init_cookie();
     if (param->systemMajor != OsVersionMajor || param->systemMinor != OsVersionMinor) return 1;
     cpu::g_systemBootTimeOffsetSeconds = param->bootTimeSeconds;

     param =
         reinterpret_cast<arch::LoaderParameterBlock*>(reinterpret_cast<std::uintptr_t>(param) + 0xffff'8000'0000'0000);
     const auto mpPage = KiInitialise(param);
     const auto start = static_cast<std::uint64_t>(KeReadHighResolutionTimerMS());

     auto framebuffer = param->framebuffer;

     std::uint32_t* buffer = reinterpret_cast<std::uint32_t*>(param->framebuffer.physicalStart + 0xffff'8000'0000'0000);

     std::uint32_t* bbuffer = static_cast<std::uint32_t*>(
         g_kernelProcess->AllocateVirtualMemory(nullptr, framebuffer.totalSize,
                                                memory::AllocationFlags::Commit | memory::AllocationFlags::Reserve |
                                                    memory::AllocationFlags::ImmediatePhysical,
                                                memory::MemoryProtection::ReadWrite));

     memory::KiHeapInitialise();

     VidInitialise(VdiFrameBuffer{.framebuffer = buffer,
                                  .width = framebuffer.width,
                                  .height = framebuffer.height,
                                  .scalineSize = framebuffer.scanlineSize,
                                  .optionalBackbuffer = bbuffer},
                   operator new);
     KeDrawBgrt();
     VidExchangeBuffers();

     object::KeInitialiseOb();
     auto* self =
         process::KiInitialiseTaskScheduler(reinterpret_cast<void*>(KiIdleLoop), param->stackVirtualBase, true);
     self->stackSize = g_loaderBlock->stackSize;
     process::KeCurrentCpu()->irql.store(cpu::IRQL::Passive, std::memory_order::release);

     UpdateProgressBar(); // CPU
     KeInitialisePCIE();
     UpdateProgressBar(); // PCIe
     g_idleCounter.store(0, std::memory_order::release);
     const auto count = KeDetectAndInitialiseProcessors(mpPage);
     debugging::DbgWrite(u8"Detected {} processors\r\n", count);
     UpdateProgressBar(); // MP

     while (g_idleCounter.load(std::memory_order::relaxed) < count) operations::Halt();

     UpdateProgressBar(); // MP lock

#ifdef ARCH_X8664
     constexpr cpu::InterruptVector SerialVector = 0x24;
     KeRegisterInterruptHandler(0x4, SerialVector, SerialInterruptHandler);
#endif
     UpdateProgressBar(); // Serial
     operations::InitialiseSerial();

     synchronisation::SpinLock* lock = new synchronisation::SpinLock{};

     UpdateProgressBar(); // Done
     const auto end = static_cast<std::uint64_t>(KeReadHighResolutionTimerMS());
     const auto initTime = end - start;
     debugging::DbgWrite(u8"Initialisation took {} ms\r\n", initTime);
     process::KeExitCurrentThread();
     while (true)
     {
          VidExchangeBuffers();

          std::array<char, 64> lineBuffer{};
          debugging::DbgWrite(u8"> ");
          const auto len = debugging::ReadSerialLine(lineBuffer.data(), lineBuffer.size());
          debugging::DbgWrite(u8"\r\n");
          if (len == 0) continue;
          std::string_view line{lineBuffer.data(), len};

          switch (line[0])
          {
          case 'm':
          {
               auto& vad = g_kernelProcess->GetVadAllocator();
               debugging::DbgWrite(u8"==== VAD Allocation Map ====\r\n");
               vad.InorderTraversal(vad.GetRoot(), PrintVadEntryCallback);
               debugging::DbgWrite(u8"==== End of VAD Map ====\r\n");
          }
          break;

          case 's': PrintVMStats(); break;

          case 'p': PrintPhysicalStats(); break;
          case 't':
          {
               const auto millisecondsSinceUnixEpoch = KeCurrentSystemTime();
               std::uint64_t totalSeconds = millisecondsSinceUnixEpoch / 1000;
               std::uint64_t second = totalSeconds % 60;
               std::uint64_t totalMinutes = totalSeconds / 60;
               std::uint64_t minute = totalMinutes % 60;
               std::uint64_t totalHours = totalMinutes / 60;
               std::uint64_t hour = totalHours % 24;
               std::uint64_t totalDays = totalHours / 24;

               // Convert days since epoch to year/month/day
               auto isLeap = [](std::uint32_t y) { return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0); };

               std::uint32_t year = 1970;
               while (true)
               {
                    std::uint64_t daysInYear = isLeap(year) ? 366 : 365;
                    if (totalDays < daysInYear) break;
                    totalDays -= daysInYear;
                    year++;
               }

               constexpr std::uint8_t daysInMonth[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
               std::uint32_t month = 1;
               for (std::uint32_t m = 0; m < 12; m++)
               {
                    std::uint64_t dim = daysInMonth[m] + (m == 1 && isLeap(year) ? 1 : 0);
                    if (totalDays < dim) break;
                    totalDays -= dim;
                    month++;
               }
               std::uint32_t day = static_cast<std::uint32_t>(totalDays) + 1;

               debugging::DbgWrite(u8"{}.{}.{} {}:{}:{}\r\n", day, month, year, hour, minute, second);
               break;
          }

          case 'v':
          {
               auto printVAD = [](const memory::VADEntry& entry)
               {
                    debugging::DbgWrite(u8"Base={}  Size=", reinterpret_cast<void*>(entry.baseAddress));
                    PrintSize(entry.size);

                    debugging::DbgWrite(u8"  State={}  Prot={}  Use={}  {}\r\n", StateToStr(entry.state),
                                        ProtToStr(entry.protection), UseToStr(entry.use),
                                        entry.immediatePhysical ? u8"Immediate" : u8"Demand");
               };

               g_kernelProcess->GetVadAllocator().InorderTraversal(g_kernelProcess->GetVadAllocator().GetRoot(),
                                                                   printVAD);

               break;
          }
          case 'r': // read memory
          {
               if (line.size() < 3) break;
               std::uintptr_t addr{};
               auto [ptr, ec] = std::from_chars(&line[2], &line[line.size()], addr, 16);
               if (ec != std::errc()) break;

               debugging::DbgWrite(u8"Address            00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F  | ASCII\r\n");

               auto readByte = [&](std::uintptr_t byteAddr) -> std::uint8_t*
               {
                    static unsigned char storage{};

                    auto* node = g_kernelProcess->GetVadAllocator().FindContaining(byteAddr);
                    if (!node) return nullptr;
                    if (node->entry.state != memory::VADMemoryState::Committed) return nullptr;
                    storage = *reinterpret_cast<unsigned char*>(byteAddr);
                    return &storage;
               };

               for (std::size_t row = 0; row < 4; row++)
               {
                    std::uintptr_t rowAddr = addr + (row * 16);
                    debugging::DbgWrite(u8"{} ", reinterpret_cast<void*>(rowAddr));

                    for (std::size_t col = 0; col < 16; col++)
                    {
                         std::uintptr_t byteAddr = rowAddr + col;
                         const auto* val = readByte(byteAddr);
                         if (val != nullptr) debugging::DbgWrite(u8"{} ", std::byte{*val});
                         else
                              debugging::DbgWrite(u8"?? ");
                    }

                    debugging::DbgWrite(u8" | ");

                    for (std::size_t col = 0; col < 16; col++)
                    {
                         std::uintptr_t byteAddr = rowAddr + col;
                         const auto* val = readByte(byteAddr);
                         char8_t c = '.';
                         if (val != nullptr && *val >= 32 && *val < 127) c = static_cast<char8_t>(*val);
                         debugging::DbgWrite(u8"{}", c);
                    }

                    debugging::DbgWrite(u8"\r\n");
               }
               break;
          }
          case 'q': // query VAD
          {
               if (line.size() < 3) break;
               std::uintptr_t addr{};
               auto [ptr, ec] = std::from_chars(&line[2], &line[line.size()], addr, 16);
               if (ec != std::errc()) break;

               auto* node = g_kernelProcess->GetVadAllocator().FindContaining(addr);
               if (node)
               {
                    PrintVADHeader();

                    PrintVADRow(node->entry);
               }
               else
               {
                    debugging::DbgWrite(u8"No VAD covering {}\r\n", reinterpret_cast<void*>(addr));
               }
               break;
          }
          case 'w':
          {
               PrintVADHeader();

               auto print = [](const memory::VADEntry& e) { PrintVADRow(e); };

               g_kernelProcess->GetVadAllocator().InorderTraversal(g_kernelProcess->GetVadAllocator().GetRoot(), print);

               break;
          }
          case 'L':
          {
               const auto start = KeReadHighResolutionTimer();
               VidClearScreen(0x1111ff);
               const auto end = KeReadHighResolutionTimer();
               debugging::DbgWrite(u8"Elapsed {}us\r\n",
                                   (end - start) * 1000'0000uz / KeReadHighResolutionTimerFrequency());
               break;
          }
          case 'I':
          {
#ifdef ARCH_ARM64
#ifdef COMPILER_MSVC
               __svc(0x2c);
#else
               asm volatile("svc %0" : : "I"(44) : "memory");
#endif
#else
#ifdef COMPILER_MSVC
               __int2c();
#else
               asm volatile("int $44");
#endif
#endif
               break;
          }
          case 'l':
          {
               const auto loCurrent = KeReadLowResolutionTimer();
               const auto hiCurrent = KeReadHighResolutionTimer();
               const auto loFrequency = KeReadLowResolutionTimerFrequency();
               const auto hiFrequency = KeReadHighResolutionTimerFrequency();
               debugging::DbgWrite(u8"Low = {} at {}Hz (~{}s)\r\n", loCurrent, loFrequency,
                                   loFrequency == 0 ? 0 : loCurrent / loFrequency);
               debugging::DbgWrite(u8"High = {} at {}Hz (~{}s)\r\n", hiCurrent, hiFrequency,
                                   hiFrequency == 0 ? 0 : hiCurrent / hiFrequency);
               break;
          }
          case 'a':
          {
               const auto start = KeReadHighResolutionTimer();
               std::memset(memory::ExAllocatePool(memory::PoolType::NonPaged, 64, 'tst1'), 0xcd, 64);
               const auto end = KeReadHighResolutionTimer();
               debugging::DbgWrite(u8"Elapsed {}us\r\n",
                                   (end - start) * 1000'0000uz / KeReadHighResolutionTimerFrequency());
          }
          break;
          case 'A':
          {
               const auto start = KeReadHighResolutionTimer();
               std::memset(memory::ExAllocatePool(memory::PoolType::NonPaged, 64, 'tst2'), 0xcd, 64);
               const auto end = KeReadHighResolutionTimer();
               debugging::DbgWrite(u8"Elapsed {}us\r\n",
                                   (end - start) * 1000'0000uz / KeReadHighResolutionTimerFrequency());
          }
          break;
          case 'b':
          {
               const auto start = KeReadHighResolutionTimer();
               std::memset(memory::ExAllocatePool(memory::PoolType::NonPaged, 4096, 'tst1'), 0xcd, 4096);
               const auto end = KeReadHighResolutionTimer();
               debugging::DbgWrite(u8"Elapsed {}us\r\n",
                                   (end - start) * 1000'0000uz / KeReadHighResolutionTimerFrequency());
          }
          break;
          case 'B':
          {
               const auto start = KeReadHighResolutionTimer();
               std::memset(memory::ExAllocatePool(memory::PoolType::NonPaged, 4096, 'tst2'), 0xcd, 4096);
               const auto end = KeReadHighResolutionTimer();
               debugging::DbgWrite(u8"Elapsed {}us\r\n",
                                   (end - start) * 1000'0000uz / KeReadHighResolutionTimerFrequency());
          }
          break;

          case 'O':
          {
               const auto typeName = [](object::ObjectType t) -> const char8_t*
               {
                    switch (t)
                    {
                    case object::ObjectType::Process: return u8"Process";
                    case object::ObjectType::Thread: return u8"Thread";
                    case object::ObjectType::File: return u8"File";
                    case object::ObjectType::Event: return u8"Event";
                    case object::ObjectType::Mutex: return u8"Mutex";
                    case object::ObjectType::Semaphore: return u8"Semaphore";
                    case object::ObjectType::Timer: return u8"Timer";
                    case object::ObjectType::Section: return u8"Section";
                    case object::ObjectType::Port: return u8"Port";
                    case object::ObjectType::SymbolicLink: return u8"SymLink";
                    case object::ObjectType::Directory: return u8"Directory";
                    case object::ObjectType::Device: return u8"Device";
                    case object::ObjectType::Driver: return u8"Driver";
                    case object::ObjectType::TypeDescriptor: return u8"TypeDesc";
                    case object::ObjectType::Processor: return u8"Processor";
                    default: return u8"Unknown";
                    }
               };

               struct PrintCtx
               {
                    char indent[64]{};
                    std::size_t indentLen{};
                    const decltype(typeName)& typeNameFn; // NOLINT
               };

               constexpr std::size_t kMaxChildren = 256;

               struct PrintNodeCtx
               {
                    const object::ObjectHeader* children[kMaxChildren]{};
                    std::size_t childCount{};
               };

               struct Printer
               {
                    static void PrintNode(const object::ObjectHeader* node, char* indent, std::size_t& indentLen,
                                          const decltype(typeName)& typeNameFn, std::uint32_t depth) noexcept
                    {
                         if (depth > 16)
                         {
                              debugging::DbgWrite(u8"{}... (depth limit)\r\n", indent);
                              return;
                         }

                         if (node->type == object::ObjectType::Directory)
                         {
                              struct SnapCtx
                              {
                                   const object::ObjectHeader** children;
                                   std::size_t* count;
                              };

                              const object::ObjectHeader* children[kMaxChildren]{};
                              std::size_t childCount = 0;
                              SnapCtx snapCtx{.children = children, .count = &childCount};

                              node->BodyAs<object::DirectoryBody>()->Enumerate(
                                  [](const object::ObjectHeader* child, void* ctx) noexcept
                                  {
                                       auto* s = static_cast<SnapCtx*>(ctx);
                                       if (*s->count < kMaxChildren) s->children[(*s->count)++] = child;
                                  },
                                  &snapCtx);

                              for (std::size_t i = 0; i < childCount; ++i)
                              {
                                   const object::ObjectHeader* child = children[i];
                                   const bool last = (i == childCount - 1);

                                   debugging::DbgWrite(u8"{}{} [{}] \"{}\"", reinterpret_cast<const char8_t*>(indent),
                                                       last ? u8"└──" : u8"├──", typeNameFn(child->type),
                                                       child->accessName.View());

                                   if (child->type == object::ObjectType::SymbolicLink)
                                   {
                                        const auto* sl = child->BodyAs<object::SymbolicLinkBody>();
                                        debugging::DbgWrite(u8" -> \"{}\"", sl->targetPath);
                                   }
                                   else if (child->type == object::ObjectType::TypeDescriptor)
                                   {
                                        const auto* td = child->BodyAs<object::TypeDescriptorBody>();
                                        debugging::DbgWrite(u8" (typeId={})", static_cast<std::uint16_t>(td->typeId));
                                   }
                                   else if (child->type == object::ObjectType::Processor)
                                   {
                                        const auto* pr = child->BodyAs<object::ProcessorBody>();
                                        debugging::DbgWrite(u8" (CPU{} APIC={} {})", pr->logicalIndex, pr->apicId,
                                                            pr->isBsp ? u8"BSP" : u8"AP");
                                   }
                                   else if (child->type == object::ObjectType::Device)
                                   {
                                        const auto scanPCI = [](this auto&& self, const device::Device* device) -> void
                                        {
                                             if (device->type == device::DeviceType::PCI)
                                             {
                                                  const auto* pciDev = static_cast<const PCIDevice*>(device);
                                                  if (pciDev->parent)
                                                  {
                                                       debugging::DbgWrite(u8" PCI {}:{}:{}.{} parent =",
                                                                           pciDev->segment, pciDev->bus, pciDev->device,
                                                                           pciDev->function);
                                                       self(pciDev->parent);
                                                  }
                                                  else
                                                       debugging::DbgWrite(u8" PCI {}:{}:{}.{}", pciDev->segment,
                                                                           pciDev->bus, pciDev->device,
                                                                           pciDev->function);
                                             }
                                             else
                                             {
                                                  debugging::DbgWrite(u8" (Device)\r\n");
                                             }
                                        };

                                        const auto* dev = child->BodyAs<device::Device>();
                                        scanPCI(dev);
                                   }

                                   debugging::DbgWrite(u8"  refs={}\r\n",
                                                       child->referenceCount.load(std::memory_order::relaxed));

                                   if (child->type == object::ObjectType::Directory)
                                   {
                                        const char8_t* ext = last ? u8"    " : u8"│   ";
                                        std::size_t extLen = 0;
                                        while (ext[extLen]) ++extLen;

                                        if (indentLen + extLen + 1 < 64)
                                        {
                                             std::memcpy(indent + indentLen, reinterpret_cast<const char*>(ext),
                                                         extLen);
                                             indentLen += extLen;
                                             indent[indentLen] = '\0';

                                             PrintNode(child, indent, indentLen, typeNameFn, depth + 1);

                                             indentLen -= extLen;
                                             indent[indentLen] = '\0';
                                        }
                                   }
                              }

                              if (childCount == 0)
                                   debugging::DbgWrite(u8"{}(empty)\r\n", reinterpret_cast<const char8_t*>(indent));
                         }
                    }
               };

               if (!object::g_rootDirectoryHeader)
               {
                    debugging::DbgWrite(u8"Object namespace not initialised.\r\n");
                    break;
               }

               debugging::DbgWrite(u8"\r\nObject Namespace\r\n");
               debugging::DbgWrite(u8"[Directory] \"\\\"  refs={}\r\n",
                                   object::g_rootDirectoryHeader->referenceCount.load(std::memory_order::relaxed));

               char indent[64]{};
               std::size_t indentLen = 0;

               Printer::PrintNode(object::g_rootDirectoryHeader, indent, indentLen, typeName, 0);

               debugging::DbgWrite(u8"\r\n");
               break;
          }

          case 'c':
          {
               const auto start = KeReadHighResolutionTimer();
               std::memset(memory::ExAllocatePool(memory::PoolType::NonPaged, 1024uz * 1024uz * 8, 'tst1'), 0xcd,
                           1024uz * 1024uz * 8);
               const auto end = KeReadHighResolutionTimer();
               debugging::DbgWrite(u8"Elapsed {}us\r\n",
                                   (end - start) * 1000'0000uz / KeReadHighResolutionTimerFrequency());
          }
          break;
          case 'C':
          {
               const auto start = KeReadHighResolutionTimer();
               std::memset(memory::ExAllocatePool(memory::PoolType::NonPaged, 1024uz * 1024uz * 8, 'tst2'), 0xcd,
                           1024uz * 1024uz * 8);
               const auto end = KeReadHighResolutionTimer();
               debugging::DbgWrite(u8"Elapsed {}us\r\n",
                                   (end - start) * 1000'0000uz / KeReadHighResolutionTimerFrequency());
          }
          break;
          case 'd':
          {
               const auto start = KeReadHighResolutionTimer();
               std::memset(memory::ExAllocatePool(memory::PoolType::NonPaged, 1024uz * 1024uz * 96, 'tst1'), 0xcd,
                           1024uz * 1024uz * 96);
               const auto end = KeReadHighResolutionTimer();
               debugging::DbgWrite(u8"Elapsed {}us\r\n",
                                   (end - start) * 1000'0000uz / KeReadHighResolutionTimerFrequency());
          }
          break;
          case 'D':
          {
               const auto start = KeReadHighResolutionTimer();
               std::memset(memory::ExAllocatePool(memory::PoolType::NonPaged, 1024uz * 1024uz * 96, 'tst2'), 0xcd,
                           1024uz * 1024uz * 96);
               const auto end = KeReadHighResolutionTimer();
               debugging::DbgWrite(u8"Elapsed {}us\r\n",
                                   (end - start) * 1000'0000uz / KeReadHighResolutionTimerFrequency());
          }
          break;
          case 'H':
          {
               auto printPool = [](memory::PoolType type, const char8_t* label)
               {
                    static std::size_t totalSegs{};
                    static std::size_t totalAlloc{};
                    static std::size_t totalFree{};
                    static std::size_t totalCommitted{};
                    static std::size_t totalUsed{};
                    totalSegs = totalAlloc = totalFree = totalCommitted = totalUsed = 0;

                    memory::ExEnumerateSegments(type,
                                                [](void* handle, std::size_t segSize)
                                                {
                                                     ++totalSegs;
                                                     totalCommitted += segSize;
                                                     memory::ExEnumeratePool(handle,
                                                                             [](memory::PoolHeader* hdr)
                                                                             {
                                                                                  if (hdr->isAllocated)
                                                                                  {
                                                                                       ++totalAlloc;
                                                                                       totalUsed += hdr->size;
                                                                                  }
                                                                                  else
                                                                                       ++totalFree;
                                                                             });
                                                });

                    debugging::DbgWrite(u8"\r\n");
                    debugging::DbgWrite(u8"=== {} ===\r\n", label);
                    debugging::DbgWrite(u8"  segments  : {}\r\n", totalSegs);
                    debugging::DbgWrite(u8"  committed : {} bytes\r\n", totalCommitted);
                    debugging::DbgWrite(u8"  in use    : {} bytes ({} allocs, {} free)\r\n", totalUsed, totalAlloc,
                                        totalFree);

                    if (!totalSegs)
                    {
                         debugging::DbgWrite(u8"  (no segments committed)\r\n");
                         return;
                    }

                    memory::ExEnumerateSegments(
                        type,
                        [](void* handle, std::size_t segSize)
                        {
                             auto* seg = reinterpret_cast<memory::PoolSegment*>(handle);

                             static std::size_t segAlloc{};
                             static std::size_t segFree{};
                             static std::size_t segUsed{};
                             static std::size_t segBlocks{};
                             static std::size_t binIdx{};
                             segAlloc = segFree = segUsed = segBlocks = binIdx = 0;
                             memory::ExEnumeratePool(handle,
                                                     [](memory::PoolHeader* hdr)
                                                     {
                                                          if (segBlocks++ == 0) binIdx = hdr->binIndex;
                                                          if (hdr->isAllocated)
                                                          {
                                                               ++segAlloc;
                                                               segUsed += hdr->size;
                                                          }
                                                          else
                                                               ++segFree;
                                                     });

                             const std::size_t usable = static_cast<std::size_t>(seg->endAddress - seg->startAddress);
                             const std::size_t pct = usable ? segUsed * 100 / usable : 0;

                             debugging::DbgWrite(u8"\r\n");
                             debugging::DbgWrite(
                                 u8"  segment {} : bin {} : {} bytes usable : {}% used : {} alloc {} free\r\n", handle,
                                 binIdx, usable, pct, segAlloc, segFree);
                             debugging::DbgWrite(u8"\r\n");

                             if (!segBlocks)
                             {
                                  debugging::DbgWrite(u8"    (no carved blocks)\r\n");
                                  return;
                             }

                             debugging::DbgWrite(
                                 u8"    header                user ptr              size        tag    state\r\n");
                             debugging::DbgWrite(
                                 u8"    --------------------  --------------------  ----------  -----  ---------\r\n");

                             memory::ExEnumeratePool(
                                 handle,
                                 [](memory::PoolHeader* hdr)
                                 {
                                      char8_t tag[5]{};
                                      std::memcpy(tag, &hdr->tag, 4);
                                      const void* userPtr = hdr + 1;
                                      const char8_t* state = hdr->isAllocated ? u8"ALLOCATED" : u8"free";
                                      debugging::DbgWrite(u8"    {}    {}    {}          {}   {}\r\n", hdr, userPtr,
                                                          hdr->size, tag, state);
                                 });
                        });
               };

               printPool(memory::PoolType::NonPaged, u8"Non-paged pool");
               printPool(memory::PoolType::Paged, u8"Paged pool");
               debugging::DbgWrite(u8"\r\n");
               break;
          }
          case 'h': // help
               debugging::DbgWrite(u8"Commands:\r\n");
               debugging::DbgWrite(u8"m - full physical memory map\r\n");
               debugging::DbgWrite(u8"s - virtual memory statistics\r\n");
               debugging::DbgWrite(u8"p - physical memory counters\r\n");
               debugging::DbgWrite(u8"v - list all VAD allocations\r\n");
               debugging::DbgWrite(u8"r <addr> - read 64 bytes from memory\r\n");
               debugging::DbgWrite(u8"h - help\r\n");
               break;
          default: debugging::DbgWrite(u8"Unknown command {}\r\n", line); break;
          }
     }

     KiIdleLoop();
     return 0;
}
extern "C" int _purecall() // NOLINT
{
     operations::DisableInterrupts();
     while (true) operations::Halt();
}
__declspec(dllexport) extern "C" int _fltused = 1;

struct SourceLocation
{
     const char* filename;
     std::uint32_t line;
     std::uint32_t column;
};
struct TypeDescriptor
{
     std::uint16_t kind;
     std::uint16_t info;
     char name[];
};
struct FunctionTypeMismatchData
{
     SourceLocation location;
     TypeDescriptor* type;
};

// UBSAN

extern "C" void __ubsan_handle_function_type_mismatch(const FunctionTypeMismatchData* data, std::uintptr_t pointer)
{
     debugging::DbgWrite(
         u8"UBSAN: Function type mismatch at {}:{}:{} - pointer {:x} does not match expected type {}\r\n",
         reinterpret_cast<const char8_t*>(data->location.filename), data->location.line, data->location.column, pointer,
         reinterpret_cast<const char8_t*>(data->type->name));
}
// __ubsan_handle_builtin_unreachable
extern "C" void __ubsan_handle_builtin_unreachable(const SourceLocation* location)
{
     debugging::DbgWrite(u8"UBSAN: Reached unreachable code at {}:{}:{}\r\n",
                         reinterpret_cast<const char8_t*>(location->filename), location->line, location->column);
}
// __ubsan_handle_divrem_overflow
extern "C" void __ubsan_handle_divrem_overflow(const SourceLocation* location)
{
     debugging::DbgWrite(u8"UBSAN: Division overflow at {}:{}:{}\r\n",
                         reinterpret_cast<const char8_t*>(location->filename), location->line, location->column);
}
// __ubsan_handle_load_invalid_value
extern "C" void __ubsan_handle_load_invalid_value(const SourceLocation* location, std::uintptr_t value)
{
     debugging::DbgWrite(u8"UBSAN: Load of invalid value {:x} at {}:{}:{}\r\n", value,
                         reinterpret_cast<const char8_t*>(location->filename), location->line, location->column);
}
// __ubsan_handle_nonnull_return_v1
extern "C" void __ubsan_handle_nonnull_return_v1(const SourceLocation* location)
{
     debugging::DbgWrite(u8"UBSAN: Non-null return violation at {}:{}:{}\r\n",
                         reinterpret_cast<const char8_t*>(location->filename), location->line, location->column);
}
// __ubsan_handle_add_overflow
extern "C" void __ubsan_handle_add_overflow(const SourceLocation* location, std::uintptr_t lhs, std::uintptr_t rhs)
{
     debugging::DbgWrite(u8"UBSAN: Addition overflow of {:x} + {:x} at {}:{}:{}\r\n", lhs, rhs,
                         reinterpret_cast<const char8_t*>(location->filename), location->line, location->column);
}
// __ubsan_handle_sub_overflow
extern "C" void __ubsan_handle_sub_overflow(const SourceLocation* location, std::uintptr_t lhs, std::uintptr_t rhs)
{
     debugging::DbgWrite(u8"UBSAN: Subtraction overflow of {:x} - {:x} at {}:{}:{}\r\n", lhs, rhs,
                         reinterpret_cast<const char8_t*>(location->filename), location->line, location->column);
}
// __ubsan_handle_mul_overflow
extern "C" void __ubsan_handle_mul_overflow(const SourceLocation* location, std::uintptr_t lhs, std::uintptr_t rhs)
{
     debugging::DbgWrite(u8"UBSAN: Multiplication overflow of {:x} * {:x} at {}:{}:{}\r\n", lhs, rhs,
                         reinterpret_cast<const char8_t*>(location->filename), location->line, location->column);
}
// __ubsan_handle_float_cast_overflow
extern "C" void __ubsan_handle_float_cast_overflow(const SourceLocation* location, std::uintptr_t from,
                                                   std::uintptr_t to)
{
     debugging::DbgWrite(u8"UBSAN: Float cast overflow from {:x} to {:x} at {}:{}:{}\r\n", from, to,
                         reinterpret_cast<const char8_t*>(location->filename), location->line, location->column);
}
// __ubsan_handle_out_of_bounds
extern "C" void __ubsan_handle_out_of_bounds(const SourceLocation* location, std::uintptr_t index, std::uintptr_t bound)
{
     debugging::DbgWrite(u8"UBSAN: Out of bounds access of index {:x} with bound {:x} at {}:{}:{}\r\n", index, bound,
                         reinterpret_cast<const char8_t*>(location->filename), location->line, location->column);
}
// __ubsan_handle_shift_out_of_bounds
extern "C" void __ubsan_handle_shift_out_of_bounds(const SourceLocation* location, std::uintptr_t lhs,
                                                   std::uintptr_t rhs)
{
     debugging::DbgWrite(u8"UBSAN: Shift out of bounds of {:x} by {:x} at {}:{}:{}\r\n", lhs, rhs,
                         reinterpret_cast<const char8_t*>(location->filename), location->line, location->column);
}
// __ubsan_handle_type_mismatch_v1
struct TypeMismatchData
{
     SourceLocation location;
     TypeDescriptor* type;
     std::uint8_t alignment;
     std::uint8_t typeCheckKind;
};
enum struct TypeCheckKind
{
     Load = 0,
     Store = 1,
     ReferenceBinding = 2,
     MemberAccess = 3,
     ConstructorCall = 4,
     DowncastPointer = 5,
     DowncastReference = 6,
     UpcastPointer = 7,
     NonnullAssign = 8,
     DynamicTypeCheck = 9
};
const char8_t* TypeCheckKindToStr(TypeCheckKind kind)
{
     switch (kind)
     {
     case TypeCheckKind::Load: return u8"Load";
     case TypeCheckKind::Store: return u8"Store";
     case TypeCheckKind::ReferenceBinding: return u8"ReferenceBinding";
     case TypeCheckKind::MemberAccess: return u8"MemberAccess";
     case TypeCheckKind::ConstructorCall: return u8"ConstructorCall";
     case TypeCheckKind::DowncastPointer: return u8"DowncastPointer";
     case TypeCheckKind::DowncastReference: return u8"DowncastReference";
     case TypeCheckKind::UpcastPointer: return u8"UpcastPointer";
     case TypeCheckKind::NonnullAssign: return u8"NonnullAssign";
     case TypeCheckKind::DynamicTypeCheck: return u8"DynamicTypeCheck";
     default: return u8"Unknown";
     }
}
enum struct ErrorType
{
     Undefined,
     NullDereference,
     NullableDereference,
     NullWithOffsetDereference,
     MisalignedPointerDereference,
     InefficientObjectSize
};
__attribute__((no_stack_protector)) extern "C" void __ubsan_handle_type_mismatch_v1(const TypeMismatchData* data,
                                                                                    std::uintptr_t pointer)
{
     // dump it
     const auto* type = data->type;
     const auto& location = data->location;
     const auto alignment = 1uz << data->alignment;
     const auto typeCheckKind = static_cast<TypeCheckKind>(data->typeCheckKind);
     ErrorType errorType = ErrorType::Undefined;
     if (pointer == 0)
     {
          errorType = typeCheckKind == TypeCheckKind::NonnullAssign ? ErrorType::NullableDereference
                                                                    : ErrorType::NullDereference;
     }
     else if ((pointer & (alignment - 1)) != 0) { errorType = ErrorType::MisalignedPointerDereference; }
     else
     {
          errorType = ErrorType::InefficientObjectSize;
     }

     debugging::DbgWrite(
         u8"UBSAN: Type mismatch ({} of type {}) at {}:{}:{} - pointer {:x} does not point to an object of type {}\r\n",
         TypeCheckKindToStr(typeCheckKind),
         errorType == ErrorType::NullDereference                ? u8"null pointer"
         : errorType == ErrorType::NullableDereference          ? u8"null pointer with nonnull attribute"
         : errorType == ErrorType::MisalignedPointerDereference ? u8"misaligned pointer"
                                                                : u8"pointer with inefficient object size",
         reinterpret_cast<const char8_t*>(location.filename), location.line, location.column, pointer,
         reinterpret_cast<const char8_t*>(type->name));
}
// __ubsan_handle_pointer_overflow
extern "C" void __ubsan_handle_pointer_overflow(const SourceLocation* location, std::uintptr_t base,
                                                std::uintptr_t result)
{
     debugging::DbgWrite(u8"UBSAN: Pointer overflow at {}:{}:{} - base {:x} resulted in {:x}\r\n",
                         reinterpret_cast<const char8_t*>(location->filename), location->line, location->column, base,
                         result);
}

__declspec(dllexport) extern "C" std::uintptr_t __security_cookie = 0x2B992DDFA232;
__declspec(dllexport) extern "C" std::uintptr_t __security_cookie_complement = ~0x2B992DDFA232;

__declspec(noreturn) void __cdecl __report_gsfailure(_In_ uintptr_t _StackCookie)
{
     debugging::DbgWrite(u8"GS failure detected! Halting.\r\n");
     CONTEXT ctx{};
     KeCaptureContext(&ctx);
     debugging::DbgWrite(u8"RIP: {:x} RSP: {:x} RBP: {:x}\r\n", ctx.Rip, ctx.Rsp, ctx.Rbp);
     debugging::DbgWrite(u8"RAX: {:x} RBX: {:x} RCX: {:x} RDX: {:x}\r\n", ctx.Rax, ctx.Rbx, ctx.Rcx, ctx.Rdx);
     debugging::DbgWrite(
         u8"RSI: {:x} RDI: {:x} R8: {:x} R9: {:x} R10: {:x} R11: {:x} R12: {:x} R13: {:x} R14: {:x} R15: {:x}\r\n",
         ctx.Rsi, ctx.Rdi, ctx.R8, ctx.R9, ctx.R10, ctx.R11, ctx.R12, ctx.R13, ctx.R14, ctx.R15);
     debugging::DbgWrite(u8"Stack cookie value: {:x}\r\n", _StackCookie);
     __debugbreak();
}

extern std::atomic<bool> haltInProgress;

__declspec(dllexport) extern "C" void __security_check_cookie(std::uintptr_t cookie) noexcept
{
     operations::DisableInterrupts();

     while (haltInProgress.exchange(true, std::memory_order::acq_rel)) operations::Halt();
     debugging::DbgWrite(u8"Checking security cookie {:x} against expected {:x}\r\n", cookie, __security_cookie);
     haltInProgress.store(false, std::memory_order_release);
     if (cookie != __security_cookie)
     {
          while (cookie & 0xffff)
          {
               __report_gsfailure(cookie);
               cookie >>= 16;
          }
     }
     operations::EnableInterrupts();
}

__attribute__((no_stack_protector)) extern "C" void __security_init_cookie() noexcept
{
     debugging::DbgWrite(u8"Initialising security cookie\r\n");
     std::uintptr_t value = __rdtsc();

     value ^= 0xDEADBEEFCAFEBABE;

     __security_cookie = value;
     __security_cookie_complement = ~value;
     debugging::DbgWrite(u8"Security cookie initialised to {:x} with complement {:x}\r\n", __security_cookie,
                         __security_cookie_complement);
}
