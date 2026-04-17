#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include "pallocator.h"

namespace memory
{
     enum struct VADMemoryState : std::uint8_t
     {
          Free,
          Reserved,
          Committed,
          PagedOut,
     };

     enum struct AllocationFlags : std::uint8_t
     {
          None = 0,
          Reserve = 1 << 0,
          Commit = 1 << 1,
          Reset = 1 << 2,
          ImmediatePhysical = 1 << 3,
          Release = 1 << 4,
          Decommit = 1 << 5,
     };

     inline AllocationFlags operator|(AllocationFlags a, AllocationFlags b)
     {
          return static_cast<AllocationFlags>(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
     }

     inline AllocationFlags operator&(AllocationFlags a, AllocationFlags b)
     {
          return static_cast<AllocationFlags>(static_cast<std::uint32_t>(a) & static_cast<std::uint32_t>(b));
     }

     inline bool operator!(AllocationFlags a) { return static_cast<std::uint32_t>(a) == 0; }

     enum struct MemoryProtection : std::uint16_t
     {
          NoAccess = 0,
          ReadOnly = 1,
          ReadWrite = 2,
          WriteCopy = 4,
          Execute = 8,
          NoCache = 16,
          WriteCombine = 32,
          LargePages = 64,
          Global = 128,
          Guard = 256,
          ExecuteRead = Execute | ReadOnly,
          ExecuteReadWrite = Execute | ReadWrite,
          ExecuteWriteCopy = Execute | WriteCopy,
     };

     struct VADEntry
     {
          VADMemoryState state;
          MemoryProtection protection;
          PFNUse use;
          std::uintptr_t baseAddress;
          std::size_t size;
          bool immediatePhysical;

          explicit VADEntry(std::uintptr_t baseAddress, std::size_t size, VADMemoryState state,
                            MemoryProtection protection, PFNUse use, bool immediatePhysical = false)
              : baseAddress(baseAddress), size(size), state(state), protection(protection), use(use),
                immediatePhysical(immediatePhysical)
          {
          }
     };

     enum struct RBColour : std::uint8_t
     {
          Red,
          Black
     };

     struct VADNode
     {
          VADEntry entry;
          RBColour colour{RBColour::Red};
          VADNode* parent{nullptr};
          VADNode* left{nullptr};
          VADNode* right{nullptr};
          std::uintptr_t maxEnd;

          explicit VADNode(const VADEntry& entry) : entry(entry), maxEnd(entry.baseAddress + entry.size) {}

          [[nodiscard]] std::uintptr_t EndAddress() const { return entry.baseAddress + entry.size; }
     };

     struct VADNodeAllocator
     {
     private:
          void* _freeList{nullptr};
          std::size_t _nodesAllocated{0};
          std::size_t _nodesFreed{0};

          bool AllocateNodePage();

     public:
          VADNodeAllocator() = default;

          VADNode* AllocateNode(const VADEntry& entry);
          void FreeNode(VADNode* node);

          [[nodiscard]] std::size_t GetNodesAllocated() const { return _nodesAllocated; }
          [[nodiscard]] std::size_t GetNodesFreed() const { return _nodesFreed; }
          [[nodiscard]] std::size_t GetNodesInUse() const { return _nodesAllocated - _nodesFreed; }

     private:
          std::atomic<bool> _lock{false};
          void AcquireLock() noexcept;
          void ReleaseLock() noexcept;
     };

     struct VirtualMemoryStatistics
     {
          std::atomic<std::size_t> totalReservedBytes{};
          std::atomic<std::size_t> totalCommittedBytes{};
          std::atomic<std::size_t> totalImmediateBytes{};
          std::atomic<std::size_t> peakReservedBytes{};
          std::atomic<std::size_t> peakCommittedBytes{};
          std::atomic<std::size_t> commitCharge{};
          std::atomic<std::size_t> reserveOperations{};
          std::atomic<std::size_t> commitOperations{};
          std::atomic<std::size_t> decommitOperations{};
          std::atomic<std::size_t> releaseOperations{};
          std::atomic<std::size_t> failedAllocations{};
     };

     struct VirtualMemoryAllocator
     {
     private:
          VADNode* _root{nullptr};
          std::uintptr_t _searchStart{0x10000};
          VADNodeAllocator _nodeAllocator;
          VirtualMemoryStatistics _stats{};
          std::uintptr_t _pageTableRoot{0};

          void RotateLeft(VADNode* x);
          void RotateRight(VADNode* y);
          void FixInsert(VADNode* z);
          void FixDelete(VADNode* x);
          void Transplant(VADNode* u, VADNode* v);
          VADNode* Minimum(VADNode* node);
          void UpdateMaxEnd(VADNode* node);
          void UpdateMaxEndUpwards(VADNode* node);
          std::uintptr_t FindFreeRegion(std::size_t size, std::uintptr_t hint = 0);

          bool CommitMemoryRange(VADNode* node, std::uintptr_t baseAddress, std::size_t size, bool immediate);
          bool DecommitMemoryRange(VADNode* node, std::uintptr_t baseAddress, std::size_t size);
          bool SplitVADForDecommit(VADNode* node, std::uintptr_t decommitStart, std::size_t decommitSize);
          void UpdateStatistics();

          bool SplitVADForCommit(VADNode* node, std::uintptr_t commitStart, std::size_t commitSize);
          bool MapPhysicalPages(VADNode* node, std::uintptr_t baseAddress, std::size_t size);

          void FixDelete(VADNode* x, VADNode* xParent);

     public:
          VirtualMemoryAllocator() = default;
          explicit VirtualMemoryAllocator(std::uintptr_t searchStart) : _searchStart(searchStart) {}

          bool Insert(const VADEntry& entry);
          bool Remove(std::uintptr_t baseAddress);
          VADNode* Search(std::uintptr_t baseAddress);
          VADNode* FindOverlap(std::uintptr_t baseAddress, std::size_t size);

          std::uintptr_t ReserveVirtualMemory(std::size_t size, MemoryProtection protection, PFNUse use);
          std::uintptr_t ReserveVirtualMemoryAt(std::uintptr_t hint, std::size_t size, MemoryProtection protection,
                                                PFNUse use);

          bool ReserveVirtualMemoryFixed(std::uintptr_t baseAddress, std::size_t size, MemoryProtection protection,
                                         PFNUse use);

          bool ReserveVirtualMemoryFixedAsCommitted(std::uintptr_t baseAddress, std::size_t size,
                                                    MemoryProtection protection, PFNUse use);

          bool CommitMemory(std::uintptr_t baseAddress, std::size_t size, bool immediate);
          bool DecommitMemory(std::uintptr_t baseAddress, std::size_t size);

          [[nodiscard]] std::uintptr_t GetSearchStart() const { return _searchStart; }

          VADNode* FindContaining(std::uintptr_t address);
          void InorderTraversal(VADNode* node, void (*callback)(const VADEntry&));

          [[nodiscard]] VADNode* GetRoot() const { return _root; }
          [[nodiscard]] const VADNodeAllocator& GetNodeAllocator() const { return _nodeAllocator; }
          [[nodiscard]] const VirtualMemoryStatistics& GetStatistics() const { return _stats; }

          void SetPageTableRoot(std::uintptr_t pageTableRoot) { _pageTableRoot = pageTableRoot; }
          [[nodiscard]] std::uintptr_t GetPageTableRoot() const { return _pageTableRoot; }

     private:
          std::atomic<bool> _lock{false};
          void AcquireLock() noexcept;
          void ReleaseLock() noexcept;
     };

     [[nodiscard]] std::uintptr_t KeGetPhysicalAddress(void* virtualAddress);
     [[nodiscard]] inline std::uintptr_t KeGetPhysicalAddress(const volatile void* virtualAddress)
     {
          return KeGetPhysicalAddress(const_cast<void*>(virtualAddress)); // NOLINT
     }
} // namespace memory
