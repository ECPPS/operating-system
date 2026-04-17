#include "pallocator.h"
#include <algorithm>
#include <cstdint>
#include "utils/arch.h"
#include "utils/identify.h"
#include "utils/kdbg.h"
#include "utils/memory.h"
#include "utils/operations.h"

namespace memory
{
     constexpr std::size_t PageSize = 0x1000;
     constexpr std::size_t InvalidPFN = static_cast<std::size_t>(-1);

     NO_ASAN bool PhysicalMemoryAllocator::Initialise(structures::LinkedList<arch::MemoryDescriptor> memoryDescriptors,
                                                      std::uintptr_t kernelPhysicalBase,
                                                      std::uintptr_t kernelVirtualBase, std::size_t kernelSize,
                                                      std::uintptr_t& mpPage)
     {
          debugging::DbgWrite(u8"Initialising physical memory allocator...\r\n");
          std::size_t totalPages{};
          std::uintptr_t minPage = ~0ull;
          std::uintptr_t maxPage{};

          for (const auto& descriptor : memoryDescriptors)
          {
               if (descriptor.type == arch::MemoryType::Reserved) continue;

               std::uintptr_t endPage = descriptor.basePage + descriptor.baseCount;
               minPage = std::min(minPage, descriptor.basePage);
               maxPage = std::max(maxPage, endPage);
          }

          basePfn = minPage;
          databaseSize = maxPage - minPage;

          std::size_t databaseBytes = databaseSize * sizeof(PFNEntry);
          std::size_t listNodesBytes = databaseSize * sizeof(structures::SingleListEntry<PFNEntry*>);
          std::size_t totalBytes = databaseBytes + listNodesBytes;
          std::size_t totalPagesNeeded = (totalBytes + PageSize - 1) / PageSize;

          std::uintptr_t allocationAddress = 0;
          for (const auto& descriptor : memoryDescriptors)
          {
               if (descriptor.type == arch::MemoryType::Free || descriptor.type == arch::MemoryType::BootServicesData ||
                   descriptor.type == arch::MemoryType::BootServicesCode)
               {
                    if (descriptor.baseCount >= totalPagesNeeded)
                    {
                         allocationAddress = descriptor.basePage * PageSize;
                         break;
                    }
               }
          }

          if (allocationAddress == 0) return false;

          std::intptr_t physToVirtOffset = static_cast<std::intptr_t>(kernelVirtualBase);
          this->physToVirtOffset = physToVirtOffset;
          std::uintptr_t virtualAddress = allocationAddress + physToVirtOffset;

          database = reinterpret_cast<PFNEntry*>(virtualAddress);
          listNodes = reinterpret_cast<structures::SingleListEntry<PFNEntry*>*>(virtualAddress + databaseBytes);

          for (std::size_t i = 0; i < databaseSize; i++)
          {
               database[i].use = PFNUse::Unused;
               database[i].region = PFNRegion::Bad;
               database[i].referenceCount = 0;

               listNodes[i].next = nullptr;
               listNodes[i].data = &database[i];
          }

          std::size_t stolenStartPfn = allocationAddress / PageSize;
          std::size_t stolenEndPfn = stolenStartPfn + totalPagesNeeded;

          for (const auto& descriptor : memoryDescriptors)
          {
               PFNRegion region = PFNRegion::Bad;
               PFNUse use = PFNUse::Unused;

               switch (descriptor.type)
               {
               case arch::MemoryType::Free:
                    region = PFNRegion::Free;
                    use = PFNUse::Unused;
                    break;
               case arch::MemoryType::PersistentMemory:
               case arch::MemoryType::BootServicesCode:
               case arch::MemoryType::BootServicesData:
               case arch::MemoryType::RuntimeServicesCode:
               case arch::MemoryType::RuntimeServicesData:
                    region = PFNRegion::Free;
                    use = PFNUse::Unused;
                    break;
               case arch::MemoryType::LoaderData:
               case arch::MemoryType::LoaderCode:
                    region = PFNRegion::Active;
                    use = PFNUse::PageTable;
                    break;
               case arch::MemoryType::ACPIReclaimMemory:
                    region = PFNRegion::Active;
                    use = PFNUse::NonPagedPool;
                    break;
               case arch::MemoryType::Reserved:
               case arch::MemoryType::MemoryMappedIO:
               case arch::MemoryType::MemoryMappedIOPortSpace:
               case arch::MemoryType::ACPIMemoryNVS:
               case arch::MemoryType::PalCode:
                    region = PFNRegion::Active;
                    use = PFNUse::NonPagedPool;
                    break;
               case arch::MemoryType::UnusableMemory:
               case arch::MemoryType::Bad:
               default:
                    region = PFNRegion::Active; // TODO: Change to Bad once resolved
                    use = PFNUse::Unused;
                    break;
               }

               for (std::size_t i = 0; i < descriptor.baseCount; ++i)
               {
                    std::size_t pfn = descriptor.basePage + i;
                    if (pfn < basePfn) continue;
                    std::size_t dbIdx = pfn - basePfn;
                    if (dbIdx >= databaseSize) continue;

                    if (pfn >= stolenStartPfn && pfn < stolenEndPfn)
                    {
                         database[dbIdx].use = PFNUse::KernelHeap;
                         database[dbIdx].region = PFNRegion::Active;
                         database[dbIdx].referenceCount = 1;

                         activePages.fetch_add(1, std::memory_order::relaxed);
                         kernelHeapPages.fetch_add(1, std::memory_order::relaxed);
                         continue;
                    }
                    if (mpPage == ~0 && pfn * PageSize < 1024uz * 1024uz)
                    {
                         mpPage = pfn * PageSize;
                         database[dbIdx].use = PFNUse::KernelHeap;
                         database[dbIdx].region = PFNRegion::Active;
                         database[dbIdx].referenceCount = 1;

                         activePages.fetch_add(1, std::memory_order::relaxed);
                         kernelHeapPages.fetch_add(1, std::memory_order::relaxed);
                         continue;
                    }

                    database[dbIdx].use = use;
                    database[dbIdx].region = region;
                    database[dbIdx].referenceCount = 0;

                    if (region == PFNRegion::Free)
                    {
                         freeList.Push(&listNodes[dbIdx]);
                         freePages.fetch_add(1, std::memory_order::relaxed);
                    }
                    else if (region == PFNRegion::Bad)
                    {
                         badList.Push(&listNodes[dbIdx]);
                         badPages.fetch_add(1, std::memory_order::relaxed);
                    }

                    if (use == PFNUse::Unused) unusedPages.fetch_add(1, std::memory_order::relaxed);
               }
          }

          return true;
     }

     NO_ASAN void PhysicalMemoryAllocator::MarkExistingPageTablesPreInit()
     {
          std::uintptr_t pageTableRootPhys = memory::paging::GetCurrentPageTable();

          pageTableRootPhys &= ~0xFFFull;

          std::size_t rootPfn = pageTableRootPhys / PageSize;
          if (rootPfn >= basePfn)
          {
               std::size_t dbIdx = rootPfn - basePfn;
               if (dbIdx < databaseSize)
               {
                    database[dbIdx].use = PFNUse::PageTable;
                    database[dbIdx].region = PFNRegion::Active;
                    database[dbIdx].referenceCount = 1;
                    activePages.fetch_add(1, std::memory_order::relaxed);
                    pageTablePages.fetch_add(1, std::memory_order::relaxed);
               }
          }

#if defined(ARCH_X8664) || defined(ARCH_ARM64)
          constexpr std::uintptr_t DirectMapOffset = 0xFFFF800000000000ULL;
#elif defined(ARCH_X86)
          constexpr std::uintptr_t DirectMapOffset = 0xC0000000UL;
#else
#error "Unknown architecture for DirectMapOffset"
#endif

#ifdef ARCH_X8664
          struct PageEntry
          {
               std::uint64_t present : 1;
               std::uint64_t writable : 1;
               std::uint64_t userAccessible : 1;
               std::uint64_t writeThrough : 1;
               std::uint64_t cacheDisable : 1;
               std::uint64_t accessed : 1;
               std::uint64_t dirty : 1;
               std::uint64_t largePage : 1;
               std::uint64_t global : 1;
               std::uint64_t available : 3;
               std::uint64_t physicalAddress : 40;
               std::uint64_t available2 : 11;
               std::uint64_t noExecute : 1;
          };

          std::uintptr_t pageTableRootVirt = pageTableRootPhys + DirectMapOffset;
          auto* pml4 = reinterpret_cast<PageEntry*>(pageTableRootVirt);

          for (std::size_t pml4Idx = 0; pml4Idx < 512; ++pml4Idx)
          {
               if (!pml4[pml4Idx].present) continue;

               std::uintptr_t pdptPhys = static_cast<std::uintptr_t>(pml4[pml4Idx].physicalAddress) << 12;

               if (pdptPhys == 0) continue;

               std::size_t pdptPfn = pdptPhys / PageSize;
               if (pdptPfn >= basePfn)
               {
                    std::size_t dbIdx = pdptPfn - basePfn;
                    if (dbIdx < databaseSize && database[dbIdx].region != PFNRegion::Active)
                    {
                         database[dbIdx].use = PFNUse::PageTable;
                         database[dbIdx].region = PFNRegion::Active;
                         database[dbIdx].referenceCount = 1;
                         activePages.fetch_add(1, std::memory_order::relaxed);
                         pageTablePages.fetch_add(1, std::memory_order::relaxed);
                    }
               }

               std::uintptr_t pdptVirt = pdptPhys + DirectMapOffset;
               auto* pdpt = reinterpret_cast<PageEntry*>(pdptVirt);

               for (std::size_t pdptIdx = 0; pdptIdx < 512; ++pdptIdx)
               {
                    if (!pdpt[pdptIdx].present) continue;
                    if (pdpt[pdptIdx].largePage) continue;

                    std::uintptr_t pdPhys = static_cast<std::uintptr_t>(pdpt[pdptIdx].physicalAddress) << 12;

                    if (pdPhys == 0) continue;

                    std::size_t pdPfn = pdPhys / PageSize;
                    if (pdPfn >= basePfn)
                    {
                         std::size_t dbIdx = pdPfn - basePfn;
                         if (dbIdx < databaseSize && database[dbIdx].region != PFNRegion::Active)
                         {
                              database[dbIdx].use = PFNUse::PageTable;
                              database[dbIdx].region = PFNRegion::Active;
                              database[dbIdx].referenceCount = 1;
                              activePages.fetch_add(1, std::memory_order::relaxed);
                              pageTablePages.fetch_add(1, std::memory_order::relaxed);
                         }
                    }

                    std::uintptr_t pdVirt = pdPhys + DirectMapOffset;
                    auto* pd = reinterpret_cast<PageEntry*>(pdVirt);

                    for (std::size_t pdIdx = 0; pdIdx < 512; ++pdIdx)
                    {
                         if (!pd[pdIdx].present) continue;
                         if (pd[pdIdx].largePage) continue;

                         std::uintptr_t ptPhys = static_cast<std::uintptr_t>(pd[pdIdx].physicalAddress) << 12;

                         if (ptPhys == 0) continue;

                         std::size_t ptPfn = ptPhys / PageSize;
                         if (ptPfn >= basePfn)
                         {
                              std::size_t dbIdx = ptPfn - basePfn;
                              if (dbIdx < databaseSize && database[dbIdx].region != PFNRegion::Active)
                              {
                                   database[dbIdx].use = PFNUse::PTE;
                                   database[dbIdx].region = PFNRegion::Active;
                                   database[dbIdx].referenceCount = 1;
                                   activePages.fetch_add(1, std::memory_order::relaxed);
                                   ptePages.fetch_add(1, std::memory_order::relaxed);
                              }
                         }
                    }
               }
          }
#elif defined(ARCH_X86)
          struct PageEntry
          {
               std::uint64_t present : 1;
               std::uint64_t writable : 1;
               std::uint64_t userAccessible : 1;
               std::uint64_t writeThrough : 1;
               std::uint64_t cacheDisable : 1;
               std::uint64_t accessed : 1;
               std::uint64_t dirty : 1;
               std::uint64_t largePage : 1;
               std::uint64_t global : 1;
               std::uint64_t available : 3;
               std::uint64_t physicalAddress : 40;
               std::uint64_t available2 : 11;
               std::uint64_t noExecute : 1;
          };

          std::uintptr_t pageTableRootVirt = pageTableRootPhys + DirectMapOffset;
          auto* pdpt = reinterpret_cast<PageEntry*>(pageTableRootVirt);

          for (std::size_t pdptIdx = 0; pdptIdx < 4; ++pdptIdx)
          {
               if (!pdpt[pdptIdx].present) continue;

               std::uintptr_t pdPhys = static_cast<std::uintptr_t>(pdpt[pdptIdx].physicalAddress) << 12;

               if (pdPhys == 0) continue;

               std::size_t pdPfn = pdPhys / PageSize;
               if (pdPfn >= basePfn)
               {
                    std::size_t dbIdx = pdPfn - basePfn;
                    if (dbIdx < databaseSize && database[dbIdx].region != PFNRegion::Active)
                    {
                         database[dbIdx].use = PFNUse::PageTable;
                         database[dbIdx].region = PFNRegion::Active;
                         database[dbIdx].referenceCount = 1;
                         activePages.fetch_add(1, std::memory_order::relaxed);
                         pageTablePages.fetch_add(1, std::memory_order::relaxed);
                    }
               }

               std::uintptr_t pdVirt = pdPhys + DirectMapOffset;
               auto* pd = reinterpret_cast<PageEntry*>(pdVirt);

               for (std::size_t pdIdx = 0; pdIdx < 512; ++pdIdx)
               {
                    if (!pd[pdIdx].present) continue;
                    if (pd[pdIdx].largePage) continue;

                    std::uintptr_t ptPhys = static_cast<std::uintptr_t>(pd[pdIdx].physicalAddress) << 12;

                    if (ptPhys == 0) continue;

                    std::size_t ptPfn = ptPhys / PageSize;
                    if (ptPfn >= basePfn)
                    {
                         std::size_t dbIdx = ptPfn - basePfn;
                         if (dbIdx < databaseSize && database[dbIdx].region != PFNRegion::Active)
                         {
                              database[dbIdx].use = PFNUse::PTE;
                              database[dbIdx].region = PFNRegion::Active;
                              database[dbIdx].referenceCount = 1;
                              activePages.fetch_add(1, std::memory_order::relaxed);
                              ptePages.fetch_add(1, std::memory_order::relaxed);
                         }
                    }
               }
          }
#elif defined(ARCH_ARM64)
          std::uintptr_t pageTableRootVirt = pageTableRootPhys + DirectMapOffset;
          auto* l0Raw = reinterpret_cast<std::uint64_t*>(pageTableRootVirt);

          for (std::size_t l0Idx = 0; l0Idx < 512; ++l0Idx)
          {
               std::uint64_t l0Entry = l0Raw[l0Idx];

               if ((l0Entry & 0x3) != 0x3) continue;

               std::uintptr_t l1Phys = (l0Entry & 0x0000FFFFFFFFF000ull);
               std::size_t l1Pfn = l1Phys / PageSize;
               if (l1Pfn >= basePfn)
               {
                    std::size_t dbIdx = l1Pfn - basePfn;
                    if (dbIdx < databaseSize && database[dbIdx].region != PFNRegion::Active)
                    {
                         database[dbIdx].use = PFNUse::PageTable;
                         database[dbIdx].region = PFNRegion::Active;
                         database[dbIdx].referenceCount = 1;
                         activePages.fetch_add(1, std::memory_order::relaxed);
                         pageTablePages.fetch_add(1, std::memory_order::relaxed);
                    }
               }

               std::uintptr_t l1Virt = l1Phys + DirectMapOffset;
               auto* l1Raw = reinterpret_cast<std::uint64_t*>(l1Virt);

               for (std::size_t l1Idx = 0; l1Idx < 512; ++l1Idx)
               {
                    std::uint64_t l1Entry = l1Raw[l1Idx];

                    if ((l1Entry & 0x3) != 0x3) continue;

                    std::uintptr_t l2Phys = (l1Entry & 0x0000FFFFFFFFF000ull);
                    std::size_t l2Pfn = l2Phys / PageSize;
                    if (l2Pfn >= basePfn)
                    {
                         std::size_t dbIdx = l2Pfn - basePfn;
                         if (dbIdx < databaseSize && database[dbIdx].region != PFNRegion::Active)
                         {
                              database[dbIdx].use = PFNUse::PageTable;
                              database[dbIdx].region = PFNRegion::Active;
                              database[dbIdx].referenceCount = 1;
                              activePages.fetch_add(1, std::memory_order::relaxed);
                              pageTablePages.fetch_add(1, std::memory_order::relaxed);
                         }
                    }

                    std::uintptr_t l2Virt = l2Phys + DirectMapOffset;
                    auto* l2Raw = reinterpret_cast<std::uint64_t*>(l2Virt);

                    for (std::size_t l2Idx = 0; l2Idx < 512; ++l2Idx)
                    {
                         std::uint64_t l2Entry = l2Raw[l2Idx];

                         if ((l2Entry & 0x3) != 0x3) continue;

                         std::uintptr_t l3Phys = (l2Entry & 0x0000FFFFFFFFF000ull);
                         std::size_t l3Pfn = l3Phys / PageSize;
                         if (l3Pfn >= basePfn)
                         {
                              std::size_t dbIdx = l3Pfn - basePfn;
                              if (dbIdx < databaseSize && database[dbIdx].region != PFNRegion::Active)
                              {
                                   database[dbIdx].use = PFNUse::PTE;
                                   database[dbIdx].region = PFNRegion::Active;
                                   database[dbIdx].referenceCount = 1;
                                   activePages.fetch_add(1, std::memory_order::relaxed);
                                   ptePages.fetch_add(1, std::memory_order::relaxed);
                              }
                         }
                    }
               }
          }
#endif
     }

     NO_ASAN void PhysicalMemoryAllocator::MarkPageActive(std::uintptr_t physicalAddress, PFNUse use)
     {
          std::size_t pfnIndex = physicalAddress / PageSize;

          if (pfnIndex < basePfn) return;
          std::size_t dbIdx = pfnIndex - basePfn;
          if (dbIdx >= databaseSize) return;

          PFNRegion oldRegion = database[dbIdx].region;
          PFNUse oldUse = database[dbIdx].use;

          if (oldRegion == PFNRegion::Free) { freePages.fetch_sub(1, std::memory_order::relaxed); }
          else if (oldRegion == PFNRegion::Zero) { zeroPages.fetch_sub(1, std::memory_order::relaxed); }
          else if (oldRegion == PFNRegion::Standby) { standbyPages.fetch_sub(1, std::memory_order::relaxed); }

          database[dbIdx].region = PFNRegion::Active;
          database[dbIdx].use = use;
          database[dbIdx].referenceCount = 1;

          if (oldRegion != PFNRegion::Active) activePages.fetch_add(1, std::memory_order::relaxed);

          DecrementUseCounter(oldUse);
          IncrementUseCounter(use);
     }

     NO_ASAN std::uintptr_t PhysicalMemoryAllocator::AllocatePage(PFNUse use)
     {
          structures::SingleListEntry<PFNEntry*>* node = zeroList.Pop();

          if (node != nullptr) zeroPages.fetch_sub(1, std::memory_order::relaxed);
          else
          {
               node = freeList.Pop();
               if (node != nullptr) freePages.fetch_sub(1, std::memory_order::relaxed);
               else
               {
                    node = standbyList.Pop();
                    if (node != nullptr) standbyPages.fetch_sub(1, std::memory_order::relaxed);
                    else
                    {
                         return ~0;
                    }
               }
          }

          std::size_t dbIdx = node - listNodes;

          PFNUse oldUse = database[dbIdx].use;
          database[dbIdx].use = use;
          database[dbIdx].region = PFNRegion::Active;
          database[dbIdx].referenceCount = 1;

          DecrementUseCounter(oldUse);
          IncrementUseCounter(use);
          activePages.fetch_add(1, std::memory_order::relaxed);

          return (dbIdx + basePfn) * PageSize;
     }

     NO_ASAN std::uintptr_t PhysicalMemoryAllocator::AllocatePageOverwrite(PFNUse use)
     {
          structures::SingleListEntry<PFNEntry*>* node = freeList.Pop();

          if (node != nullptr) freePages.fetch_sub(1, std::memory_order::relaxed);
          else
          {
               node = zeroList.Pop();
               if (node != nullptr) zeroPages.fetch_sub(1, std::memory_order::relaxed);
               else
               {
                    node = standbyList.Pop();
                    if (node != nullptr) standbyPages.fetch_sub(1, std::memory_order::relaxed);
                    else
                         return ~0;
               }
          }

          std::size_t dbIdx = node - listNodes;

          PFNUse oldUse = database[dbIdx].use;
          database[dbIdx].use = use;
          database[dbIdx].region = PFNRegion::Active;
          database[dbIdx].referenceCount = 1;

          DecrementUseCounter(oldUse);
          IncrementUseCounter(use);
          activePages.fetch_add(1, std::memory_order::relaxed);

          return (dbIdx + basePfn) * PageSize;
     }

     NO_ASAN void PhysicalMemoryAllocator::ReleaseZeroPage(std::uintptr_t address)
     {
          std::size_t pfnIndex = address / PageSize;

          if (pfnIndex < basePfn) return;
          std::size_t dbIdx = pfnIndex - basePfn;
          if (dbIdx >= databaseSize) return;

          PFNEntry& entry = database[dbIdx];

          if (entry.referenceCount > 0) entry.referenceCount--;

          if (entry.referenceCount == 0)
          {
               PFNUse oldUse = entry.use;
               PFNRegion oldRegion = entry.region;

               if (oldRegion == PFNRegion::Active) activePages.fetch_sub(1, std::memory_order::relaxed);

               DecrementUseCounter(oldUse);

               entry.use = PFNUse::Unused;
               entry.region = PFNRegion::Zero;

               zeroList.Push(&listNodes[dbIdx]);

               zeroPages.fetch_add(1, std::memory_order::relaxed);
               unusedPages.fetch_add(1, std::memory_order::relaxed);
          }
     }

     NO_ASAN void PhysicalMemoryAllocator::ReleaseFreePage(std::uintptr_t address)
     {
          std::size_t pfnIndex = address / PageSize;

          if (pfnIndex < basePfn) return;
          std::size_t dbIdx = pfnIndex - basePfn;
          if (dbIdx >= databaseSize) return;

          PFNEntry& entry = database[dbIdx];

          if (entry.referenceCount > 0) entry.referenceCount--;

          if (entry.referenceCount == 0)
          {
               PFNUse oldUse = entry.use;
               PFNRegion oldRegion = entry.region;

               if (oldRegion == PFNRegion::Active) activePages.fetch_sub(1, std::memory_order::relaxed);

               DecrementUseCounter(oldUse);

               entry.use = PFNUse::Unused;
               entry.region = PFNRegion::Free;

               freeList.Push(&listNodes[dbIdx]);

               freePages.fetch_add(1, std::memory_order::relaxed);
               unusedPages.fetch_add(1, std::memory_order::relaxed);
          }
     }

     NO_ASAN void PhysicalMemoryAllocator::IncrementUseCounter(PFNUse use, std::memory_order order)
     {
          switch (use)
          {
          case PFNUse::Unused: unusedPages.fetch_add(1, order); break;
          case PFNUse::ProcessPrivate: processPrivatePages.fetch_add(1, order); break;
          case PFNUse::MappedFile: mappedFilePages.fetch_add(1, order); break;
          case PFNUse::DriverLocked: driverLockedPages.fetch_add(1, order); break;
          case PFNUse::UserStack: userStackPages.fetch_add(1, order); break;
          case PFNUse::KernelStack: kernelStackPages.fetch_add(1, order); break;
          case PFNUse::KernelHeap: kernelHeapPages.fetch_add(1, order); break;
          case PFNUse::MetaFile: metaFilePages.fetch_add(1, order); break;
          case PFNUse::NonPagedPool: nonPagedPoolPages.fetch_add(1, order); break;
          case PFNUse::PagedPool: pagedPoolPages.fetch_add(1, order); break;
          case PFNUse::PTE: ptePages.fetch_add(1, order); break;
          case PFNUse::Shareable: shareablePages.fetch_add(1, order); break;
          case PFNUse::PageTable: pageTablePages.fetch_add(1, order); break;
          case PFNUse::FSCache: fsCachePages.fetch_add(1, order); break;
          }
     }

     NO_ASAN void PhysicalMemoryAllocator::DecrementUseCounter(PFNUse use, std::memory_order order)
     {
          switch (use)
          {
          case PFNUse::Unused: unusedPages.fetch_sub(1, order); break;
          case PFNUse::ProcessPrivate: processPrivatePages.fetch_sub(1, order); break;
          case PFNUse::MappedFile: mappedFilePages.fetch_sub(1, order); break;
          case PFNUse::DriverLocked: driverLockedPages.fetch_sub(1, order); break;
          case PFNUse::UserStack: userStackPages.fetch_sub(1, order); break;
          case PFNUse::KernelStack: kernelStackPages.fetch_sub(1, order); break;
          case PFNUse::KernelHeap: kernelHeapPages.fetch_sub(1, order); break;
          case PFNUse::MetaFile: metaFilePages.fetch_sub(1, order); break;
          case PFNUse::NonPagedPool: nonPagedPoolPages.fetch_sub(1, order); break;
          case PFNUse::PagedPool: pagedPoolPages.fetch_sub(1, order); break;
          case PFNUse::PTE: ptePages.fetch_sub(1, order); break;
          case PFNUse::Shareable: shareablePages.fetch_sub(1, order); break;
          case PFNUse::PageTable: pageTablePages.fetch_sub(1, order); break;
          case PFNUse::FSCache: fsCachePages.fetch_sub(1, order); break;
          }
     }

     NO_ASAN PhysicalMemoryAllocator physicalAllocator{};
} // namespace memory
