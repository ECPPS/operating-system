#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include "utils/arch.h"
#include "utils/struct.h"

namespace memory
{
     enum struct PFNUse : std::uint_least8_t
     {
          Unused,
          ProcessPrivate,
          MappedFile,
          DriverLocked,
          UserStack,
          KernelStack,
          KernelHeap,
          MetaFile,
          NonPagedPool,
          PagedPool,
          PTE,
          Shareable,
          PageTable,
          FSCache
     };
     enum struct PFNRegion : std::uint_least8_t
     {
          Active = 0b001,
          Standby = 0b011,
          Zero = 0b010,
          Free = 0b000,
          Modified = 0b101,
          Bad = 0b111,
     };
     struct PFNEntry
     {
          PFNUse use{};
          PFNRegion region{};
          std::uint16_t referenceCount{};
     };
     struct PhysicalMemoryAllocator
     {
          PFNEntry* database{};
          std::size_t databaseSize{};
          std::size_t basePfn{};
          structures::SingleListEntry<PFNEntry*>* listNodes{};
          std::intptr_t physToVirtOffset{};

          structures::AtomicSingleList<PFNEntry*> activeList{};
          structures::AtomicSingleList<PFNEntry*> freeList{};
          structures::AtomicSingleList<PFNEntry*> zeroList{};
          structures::AtomicSingleList<PFNEntry*> standbyList{};
          structures::AtomicSingleList<PFNEntry*> badList{};
          structures::AtomicSingleList<PFNEntry*> modifiedList{};

          std::atomic<std::size_t> activePages{};
          std::atomic<std::size_t> freePages{};
          std::atomic<std::size_t> zeroPages{};
          std::atomic<std::size_t> standbyPages{};
          std::atomic<std::size_t> badPages{};
          std::atomic<std::size_t> modifiedPages{};

          std::atomic<std::size_t> unusedPages{};
          std::atomic<std::size_t> processPrivatePages{};
          std::atomic<std::size_t> mappedFilePages{};
          std::atomic<std::size_t> driverLockedPages{};
          std::atomic<std::size_t> userStackPages{};
          std::atomic<std::size_t> kernelStackPages{};
          std::atomic<std::size_t> kernelHeapPages{};
          std::atomic<std::size_t> metaFilePages{};
          std::atomic<std::size_t> nonPagedPoolPages{};
          std::atomic<std::size_t> pagedPoolPages{};
          std::atomic<std::size_t> ptePages{};
          std::atomic<std::size_t> shareablePages{};
          std::atomic<std::size_t> pageTablePages{};
          std::atomic<std::size_t> fsCachePages{};

          bool Initialise(structures::LinkedList<arch::MemoryDescriptor> memoryDescriptors,
                          std::uintptr_t kernelPhysicalBase, std::uintptr_t kernelVirtualBase, std::size_t kernelSize,
                          std::uintptr_t& mpPage);
          void MarkPageActive(std::uintptr_t physicalAddress, PFNUse use);
          std::uintptr_t AllocatePage(PFNUse use);
          std::uintptr_t AllocatePageOverwrite(PFNUse use);
          void ReleaseZeroPage(std::uintptr_t address);
          void ReleaseFreePage(std::uintptr_t address);

     private:
          void PushToList(std::atomic<structures::SingleListEntry<PFNEntry*>*>& listHead, std::size_t pfnIndex) const;
          std::size_t PopFromList(std::atomic<structures::SingleListEntry<PFNEntry*>*>& listHead) const;
          void IncrementUseCounter(PFNUse use, std::memory_order order = std::memory_order::relaxed);
          void DecrementUseCounter(PFNUse use, std::memory_order order = std::memory_order::relaxed);
          void MarkExistingPageTablesPreInit();
     };

     extern PhysicalMemoryAllocator physicalAllocator;
} // namespace memory
