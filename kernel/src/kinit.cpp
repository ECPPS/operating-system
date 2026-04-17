#include "kinit.h"
#include "cpu/interrupts.h"
#include "utils/identify.h"
#include "utils/memory.h"
#include "utils/operations.h"

std::uint64_t g_bootCycles{};
arch::LoaderParameterBlock* g_loaderBlock{};
kernel::ProcessControlBlock* g_kernelProcess{};
std::uintptr_t g_stackBase{};
std::uintptr_t g_stackSize{};
std::uintptr_t g_imageBase{};
std::uintptr_t g_imageSize{};

NO_ASAN void KeInitialiseCpu(std::uintptr_t acpiPhysical)
{
     KiInitialiseInterrupts(acpiPhysical);
     operations::EnableInterrupts();
     KeSetTimerFrequency(64);
}

NO_ASAN void KeRemoveLeftoverMappings()
{
     const auto ptl4Physical = memory::paging::GetCurrentPageTable();
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

     auto* ptl4 = reinterpret_cast<PageEntry*>(ptl4Physical + 0xffff'8000'0000'0000);
     for (std::uintptr_t i = 0; i < 255; i++)
     {
          if (!ptl4[i].present) continue;

          ptl4[i].present = 0;
     }

     memory::paging::FlushTLB();
#elifdef ARCH_ARM64
     auto* l0 = reinterpret_cast<std::uint64_t*>(ptl4Physical + 0xffff'8000'0000'0000);

     for (std::uintptr_t i = 0; i < 256; i++)
     {
          const std::uint64_t l0e = l0[i];
          if (!(l0e & 1)) continue;

          if (!(l0e & 2)) continue;

          auto* const l1 = reinterpret_cast<std::uint64_t*>((l0e & 0x0000'ffff'ffff'f000ull) + memory::virtualOffset);

          for (std::uintptr_t j = 0; j < 512; j++)
          {
               const std::uint64_t l1e = l1[j];
               if (!(l1e & 1)) continue;

               if (!(l1e & 2)) continue;

               auto* const l2 =
                   reinterpret_cast<std::uint64_t*>((l1e & 0x0000'ffff'ffff'f000ull) + memory::virtualOffset);

               for (std::uintptr_t k = 0; k < 512; k++)
               {
                    const std::uint64_t l2e = l2[k];
                    if (!(l2e & 1)) continue;

                    if (!(l2e & 2)) continue;

                    auto* const l3 =
                        reinterpret_cast<std::uint64_t*>((l2e & 0x0000'ffff'ffff'f000ull) + memory::virtualOffset);

                    for (std::uintptr_t l = 0; l < 512; l++) l3[l] = 0;

                    memory::physicalAllocator.ReleaseFreePage(l2e & 0x0000'ffff'ffff'f000ull);

                    l2[k] = 0;
               }

               memory::physicalAllocator.ReleaseFreePage(l1e & 0x0000'ffff'ffff'f000ull);

               l1[j] = 0;
          }

          memory::physicalAllocator.ReleaseFreePage(l0e & 0x0000'ffff'ffff'f000ull);

          l0[i] = 0;
     }
#endif
}
