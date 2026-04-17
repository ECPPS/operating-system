#include "mp.h"
#include <cstdint>
#include <cstring>
#include "../kinit.h"
#include "../process/process.h"
#include "../process/taskScheduler.h"
#include "../process/thread.h"
#include "interrupts.h"
#include "utils/cpu.h"
#include "utils/kdbg.h"
#include "utils/memory.h"
#include "utils/operations.h"

extern MADT* g_madt;
extern volatile std::uint32_t* g_lapic;
static std::atomic<bool> g_processorsInitialised{false};
static std::atomic<bool> g_processorsDone{false};
static std::atomic<std::uint64_t> g_processorCount{1};
static std::uint64_t g_address{};

#pragma pack(push, 1)
struct GdtEntry
{
     std::uint16_t limitLow;
     std::uint16_t baseLow;
     std::uint8_t baseMiddle;
     std::uint8_t access;
     std::uint8_t granularity;
     std::uint8_t baseHigh;
};
struct Gdtr
{
     std::uint16_t limit;
     std::uint64_t base;
};
#pragma pack(pop)

extern "C" GdtEntry sGdt[8];

inline void LapicWrite(std::uint32_t regOffset, std::uint32_t value)
{
     g_lapic[regOffset / sizeof(std::uint32_t)] = value;
}

constexpr std::uint32_t LAPIC_ID = 0x020;
constexpr std::uint32_t LAPIC_EOI = 0x0B0;
constexpr std::uint32_t LAPIC_SVR = 0x0F0;
constexpr std::uint32_t LAPIC_ERR = 0x280;
constexpr std::uint32_t LAPIC_ICR_LOW = 0x300;
constexpr std::uint32_t LAPIC_ICR_HIGH = 0x310;
constexpr std::uint32_t LAPIC_TIMER = 0x320;
constexpr std::uint32_t LAPIC_LINT0 = 0x350;
constexpr std::uint32_t LAPIC_LINT1 = 0x360;

static void KiDisable(std::uint64_t& cr, std::uint8_t bit) { cr &= ~(1ull << bit); }
static void KiEnable(std::uint64_t& cr, std::uint8_t bit) { cr |= (1ull << bit); }

__attribute__((no_stack_protector)) void KeThProcessorStartup(const std::uintptr_t stackPointer)
{
     g_processorsInitialised.store(true, std::memory_order::release);
     while (!g_processorsDone.load(std::memory_order::acquire)) operations::Yield();
     memory::paging::InvalidatePage(g_address);

     operations::DisableInterrupts();
     cpu::Initialise(nullptr);
#ifdef COMPILER_MSVC
     _lgdt(&gdtr);
#else
     asm volatile("mov  %0,    %%ss \n\t"
                  "mov  %0,    %%ds \n\t"
                  "mov  %0,    %%es \n\t"
                  "xorl %%eax, %%eax\n\t"
                  "mov  %%eax, %%fs \n\t"
                  "mov  %%eax, %%gs \n\t"
                  "lea  1f(%%rip), %%rax\n\t"
                  "push %1          \n\t"
                  "push %%rax       \n\t"
                  "lretq            \n\t"
                  "1:               \n\t" ::"r"(static_cast<std::uint64_t>(0x10)),
                  "r"(static_cast<std::uint64_t>(0x08))
                  : "rax", "memory");
#endif

     std::uint32_t eaxCpuID1{};
     std::uint32_t ebxCpuID1{};
     std::uint32_t ecxCpuID1{};
     std::uint32_t edxCpuID1{};
#ifdef COMPILER_MSVC
     {
          int i[4]{};
          __cpuid(i, 1);
          eaxCpuID1 = i[0];
          ebxCpuID1 = i[1];
          ecxCpuID1 = i[2];
          edxCpuID1 = i[3];
     }
#else
     eaxCpuID1 = 1;
     asm volatile("cpuid" : "+a"(eaxCpuID1), "=b"(ebxCpuID1), "=c"(ecxCpuID1), "=d"(edxCpuID1));
#endif

     std::uint32_t eaxCpuID7{};
     std::uint32_t ebxCpuID7{};
     std::uint32_t ecxCpuID7{};
     std::uint32_t edxCpuID7{};
#ifdef COMPILER_MSVC
     {
          int i2[4]{};
          __cpuidex(i2, 7, 0);
          eaxCpuID7 = i2[0];
          ebxCpuID7 = i2[1];
          ecxCpuID7 = i2[2];
          edxCpuID7 = i2[3];
     }
#else
     eaxCpuID7 = 7;
     ecxCpuID7 = 0;
     asm volatile("cpuid" : "+a"(eaxCpuID7), "=b"(ebxCpuID7), "+c"(ecxCpuID7), "=d"(edxCpuID7));
#endif

     const bool hasXSAVE = ecxCpuID1 & (1u << 26);
     const bool hasAVX = ecxCpuID1 & (1u << 28);
     const bool hasAVX512 = ebxCpuID7 & (1u << 16);
     const bool hasUMIP = ecxCpuID7 & (1u << 2);
     const bool hasSMEP = ebxCpuID7 & (1u << 7);
     const bool hasSMAP = ebxCpuID7 & (1u << 20);

     std::uint64_t cr0{};
#ifdef COMPILER_MSVC
     cr0 = __readcr0();
#else
     asm volatile("mov %%cr0, %0" : "=r"(cr0));
#endif
     KiEnable(cr0, 0);
     KiEnable(cr0, 1);
     KiDisable(cr0, 2);
     KiDisable(cr0, 3);
     KiEnable(cr0, 4);
     KiEnable(cr0, 5);
     KiEnable(cr0, 16);
     KiDisable(cr0, 18);
     KiEnable(cr0, 31);
#ifdef COMPILER_MSVC
     __writecr0(cr0);
#else
     asm volatile("mov %0, %%cr0" ::"r"(cr0) : "memory");
#endif

     std::uint64_t cr4{};
#ifdef COMPILER_MSVC
     cr4 = __readcr4();
#else
     asm volatile("mov %%cr4, %0" : "=r"(cr4));
#endif
     KiEnable(cr4, 5);
     KiEnable(cr4, 6);
     KiEnable(cr4, 7);
     KiEnable(cr4, 9);
     KiEnable(cr4, 10);
     if (hasUMIP) KiEnable(cr4, 11);
     if (hasXSAVE) KiEnable(cr4, 18);
     if (hasSMEP) KiEnable(cr4, 20);
     if (hasSMAP) KiEnable(cr4, 21);
#ifdef COMPILER_MSVC
     __writecr4(cr4);
#else
     asm volatile("mov %0, %%cr4" ::"r"(cr4) : "memory");
#endif

     std::uint32_t eferLow{};
     std::uint32_t eferHigh{};
#ifdef COMPILER_MSVC
     const std::uint64_t efer_val = __readmsr(0xC000'0080);
     eferLow = static_cast<std::uint32_t>(efer_val);
     eferHigh = static_cast<std::uint32_t>(efer_val >> 32);
#else
     asm volatile("rdmsr" : "=a"(eferLow), "=d"(eferHigh) : "c"(0xC000'0080u));
#endif
     std::uint64_t efer = (static_cast<std::uint64_t>(eferHigh) << 32) | eferLow;
     KiEnable(efer, 0);
     KiEnable(efer, 8);
     KiEnable(efer, 11);
     eferLow = static_cast<std::uint32_t>(efer);
     eferHigh = static_cast<std::uint32_t>(efer >> 32);
#ifdef COMPILER_MSVC
     __writemsr(0xC000'0080, efer);
#else
     asm volatile("wrmsr" ::"c"(0xC000'0080u), "a"(eferLow), "d"(eferHigh));
#endif
     if (hasXSAVE)
     {
          std::uint64_t xcr0 = (1ull << 0) | (1ull << 1);
          if (hasAVX) xcr0 |= (1ull << 2);
          if (hasAVX512) xcr0 |= (1ull << 5) | (1ull << 6) | (1ull << 7);
          const std::uint32_t lo = static_cast<std::uint32_t>(xcr0);
          const std::uint32_t hi = static_cast<std::uint32_t>(xcr0 >> 32);
#ifdef COMPILER_MSVC
          _xsetbv(0, xcr0);
#else
          asm volatile("xsetbv" ::"c"(0u), "a"(lo), "d"(hi));
#endif
     }

     KeSetTimerFrequency(64, false);
     process::KiInitialiseTaskScheduler(reinterpret_cast<void*>(KiIdleLoop),
                                        stackPointer - process::ThreadStackSize + 4096, false);

     operations::EnableInterrupts();
     g_lapic[0xF0 / 4] = 0x1FF;
     g_lapic[0xB0 / 4] = 0;

     process::KeExitCurrentThread();
}

bool KeStartApplicationProcessor(std::uint32_t apicId, std::uintptr_t address)
{
     constexpr std::size_t kPatchLgdtDisp16 = 0x05;
     constexpr std::size_t kPatchJmpPmOff16 = 0x14;
     constexpr std::size_t kPatchGdtBase = 0x1A;
     constexpr std::size_t kPatchCr3 = 0x54;
     constexpr std::size_t kPatchJmpLmOff32 = 0x75;
     constexpr std::size_t kPatchEntryImm64 = 0x7D;
     constexpr std::size_t kPatchStackImm64 = 0x87;

     alignas(16) static std::uint8_t boot[4096] // NOLINT
         = {
             0xfa, 0xfc, 0x0f, 0x01, 0x16, 0x00, 0x00, 0x0f, 0x20, 0xc0, 0x66, 0x0d, 0x01, 0x00, 0x00, 0x20, 0x0f, 0x22,
             0xc0, 0xea, 0x00, 0x00, 0x18, 0x00, 0x27, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
             0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0x00, 0x9a, 0xaf, 0x00, 0xff, 0xff, 0x00, 0x00, 0x00, 0x92, 0xcf, 0x00,
             0xff, 0xff, 0x00, 0x00, 0x00, 0x9a, 0xcf, 0x00, 0xff, 0xff, 0x00, 0x00, 0x00, 0x92, 0xcf, 0x00, 0x90, 0x90,
             0x0f, 0x20, 0xe0, 0x0d, 0x20, 0x08, 0x00, 0x00, 0x0f, 0x22, 0xe0, 0xb8, 0x00, 0x00, 0x00, 0x00, 0x0f, 0x22,
             0xd8, 0xb9, 0x80, 0x00, 0x00, 0xc0, 0x0f, 0x32, 0x0d, 0x00, 0x09, 0x00, 0x00, 0x0f, 0x30, 0x0f, 0x20, 0xc0,
             0x0d, 0x01, 0x00, 0x00, 0x80, 0x0f, 0x22, 0xc0, 0xea, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x48, 0xb8, 0x00,
             0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x48, 0xbc, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f,
             0x20, 0xd9, 0x0f, 0x22, 0xd9, 0x48, 0x89, 0xe1, 0xff, 0xd0,
         };

     static_assert(kPatchLgdtDisp16 + 2 <= sizeof(boot));
     static_assert(kPatchJmpPmOff16 + 2 <= sizeof(boot));
     static_assert(kPatchGdtBase + 4 <= sizeof(boot));
     static_assert(kPatchCr3 + 4 <= sizeof(boot));
     static_assert(kPatchJmpLmOff32 + 4 <= sizeof(boot));
     static_assert(kPatchEntryImm64 + 8 <= sizeof(boot));
     static_assert(kPatchStackImm64 + 8 <= sizeof(boot));

     const std::uint64_t bspCr3 = memory::paging::GetCurrentPageTable();
     constexpr std::uintptr_t kHhdm = 0xffff'8000'0000'0000;

     void* stackBase =
         g_kernelProcess->AllocateVirtualMemory(nullptr, process::ThreadStackSize,
                                                memory::AllocationFlags::Commit | memory::AllocationFlags::Reserve |
                                                    memory::AllocationFlags::ImmediatePhysical,
                                                memory::MemoryProtection::ReadWrite);
     const std::uintptr_t stackTop = reinterpret_cast<std::uintptr_t>(stackBase) + process::ThreadStackSize - 4096;
     const std::uintptr_t stackPointer = stackTop & ~std::uintptr_t{0xF};
     std::memset(stackBase, 0xcc, process::ThreadStackSize);
     memory::paging::ProtectPage(bspCr3, stackTop, false, false, false,
                                 [](std::size_t) -> void*
                                 {
                                      std::uintptr_t page =
                                          memory::physicalAllocator.AllocatePage(memory::PFNUse::PageTable);
                                      if (page == ~0uz) return nullptr;
                                      return reinterpret_cast<void*>(page + memory::virtualOffset);
                                 });
     memory::paging::InvalidatePage(stackTop);

     std::uint16_t value16 = 0x18;
     std::memcpy(&boot[kPatchLgdtDisp16], &value16, 2);

     value16 = 0x48;
     std::memcpy(&boot[kPatchJmpPmOff16], &value16, 2);

     auto value32 = static_cast<std::uint32_t>(address + 0x1E);
     std::memcpy(&boot[kPatchGdtBase], &value32, 4);

     value32 = static_cast<std::uint32_t>(bspCr3);
     std::memcpy(&boot[kPatchCr3], &value32, 4);

     value32 = static_cast<std::uint32_t>(address + 0x7B);
     std::memcpy(&boot[kPatchJmpLmOff32], &value32, 4);

     auto value64 = reinterpret_cast<std::uint64_t>(&KeThProcessorStartup);
     std::memcpy(&boot[kPatchEntryImm64], &value64, 8);

     value64 = static_cast<std::uint64_t>(stackPointer);
     std::memcpy(&boot[kPatchStackImm64], &value64, 8);

     std::memcpy(reinterpret_cast<void*>(address + kHhdm), boot, sizeof(boot));

     LapicWrite(LAPIC_ICR_HIGH, apicId << 24);
     LapicWrite(LAPIC_ICR_LOW, 0xC500);
     while (g_lapic[0x300 / 4] & (1u << 12)) operations::Yield();

     LapicWrite(LAPIC_ICR_HIGH, apicId << 24);
     LapicWrite(LAPIC_ICR_LOW, 0x4500);
     while (g_lapic[0x300 / 4] & (1u << 12)) operations::Yield();

     process::KeSleepCurrentThread(10);

     LapicWrite(LAPIC_ERR, 0);
     LapicWrite(LAPIC_ICR_HIGH, apicId << 24);
     LapicWrite(LAPIC_ICR_LOW, 0x4600 | static_cast<std::uint32_t>(address >> 12));
     process::KeSleepCurrentThread(1);
     while (g_lapic[0x300 / 4] & (1u << 12)) operations::Yield();

     LapicWrite(LAPIC_ERR, 0);
     LapicWrite(LAPIC_ICR_HIGH, apicId << 24);
     LapicWrite(LAPIC_ICR_LOW, 0x4600 | static_cast<std::uint32_t>(address >> 12));
     process::KeSleepCurrentThread(1);
     while (g_lapic[0x300 / 4] & (1u << 12)) operations::Yield();

     // memory::paging::UnmapPage(bspCr3, address);

     return true;
}

void KeSendIPI(std::uint32_t apicId, std::uint32_t vector)
{
     LapicWrite(LAPIC_ICR_HIGH, apicId << 24);
     LapicWrite(LAPIC_ICR_LOW, (0b000 << 8) | vector);
}

std::uint64_t KeDetectAndInitialiseProcessors(std::uintptr_t address)
{
     g_address = address;
     if (g_madt == nullptr) return 0;

     std::uint32_t processorCount = 0;

     const std::uint8_t* ptr = g_madt->entries;
     const std::uint8_t* end = reinterpret_cast<const std::uint8_t*>(g_madt) + g_madt->header.length;
     const std::uint8_t bspApicId = static_cast<std::uint8_t>(g_lapic[0x20 / 4] >> 24);

     debugging::DbgWrite(u8"BSP = {}\r\n", bspApicId);

     memory::paging::MapPage(memory::paging::GetCurrentPageTable(),
                             memory::PageMapping{
                                 .virtualAddress = address,
                                 .physicalAddress = address,
                                 .size = 0x1000,
                                 .writable = true,
                                 .executable = true,
                                 .cachePolicy = memory::CachePolicy::Uncacheable,
                             },
                             [](std::size_t) -> void*
                             {
                                  std::uintptr_t page =
                                      memory::physicalAllocator.AllocatePage(memory::PFNUse::PageTable);
                                  if (page == ~0uz) return nullptr;
                                  return reinterpret_cast<void*>(page + memory::virtualOffset);
                             });
     memory::paging::InvalidatePage(address);

     g_processorsDone.store(false, std::memory_order::release);
     while (ptr < end)
     {
          const auto* entry = reinterpret_cast<const MADTEntry*>(ptr);

          if (entry->type == 0)
          {
               const auto* lapic = reinterpret_cast<const MADTEntryLAPIC*>(entry);
               const bool cpuEnabled = (lapic->flags & 1) != 0;
               const bool cpuOnlineCapable = (lapic->flags & 2) != 0;

               if (lapic->apicId != bspApicId && (cpuEnabled || cpuOnlineCapable))
               {
                    debugging::DbgWrite(u8"Starting CPU {}...\r\n", lapic->apicId);

                    g_processorsInitialised.store(false, std::memory_order::release);
                    KeStartApplicationProcessor(lapic->apicId, address);

                    while (!g_processorsInitialised.load(std::memory_order::acquire)) operations::Halt();

                    debugging::DbgWrite(u8"Executed CPU {} (online-capable {})\r\n", lapic->apicId, cpuOnlineCapable);
                    processorCount++;
               }
          }

          ptr += entry->length;
     }
     memory::paging::UnmapPage(memory::paging::GetCurrentPageTable(), address);
     g_processorsDone.store(true, std::memory_order::release);

     g_processorCount.store(processorCount + 1, std::memory_order::release);
     return processorCount;
}

std::uint64_t KeCPUCount() { return g_processorCount.load(std::memory_order::relaxed); }
