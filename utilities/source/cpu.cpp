#include <utils/cpu.h>
#include <utils/identify.h>
#include <utils/operations.h>
#include "utils/kdbg.h"

#if defined(ARCH_ARM64) && defined(COMPILER_MSVC)
#include <intrin.h>
#endif

#ifdef ARCH_X8664 // x86-64 vvv

extern "C" void ReloadCSCode();

namespace cpu
{
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
#pragma pack(pop)

#pragma pack(push, 1)
     struct Tss64
     {
          std::uint32_t reserved0;
          std::uint64_t rsp0;
          std::uint64_t rsp1;
          std::uint64_t rsp2;
          std::uint64_t reserved1;
          std::uint64_t ist1;
          std::uint64_t ist2;
          std::uint64_t ist3;
          std::uint64_t ist4;
          std::uint64_t ist5;
          std::uint64_t ist6;
          std::uint64_t ist7;
          std::uint64_t reserved2;
          std::uint16_t reserved3;
          std::uint16_t iomapBase;
     };
#pragma pack(pop)

#pragma pack(push, 1)
     struct IdtEntry
     {
          std::uint16_t offsetLow;
          std::uint16_t selector;
          std::uint8_t ist;
          std::uint8_t typeAttr;
          std::uint16_t offsetMiddle;
          std::uint32_t offsetHigh;
          std::uint32_t reserved;
     };
#pragma pack(pop)

#pragma pack(push, 1)
     struct Gdtr
     {
          std::uint16_t limit;
          std::uint64_t base;
     };
#pragma pack(pop)

#pragma pack(push, 1)
     struct Idtr
     {
          std::uint16_t limit;
          std::uint64_t base;
     };
#pragma pack(pop)
} // namespace cpu

namespace cpu
{
     cpu::GdtEntry* lpGdt;
     cpu::Tss64* lpTss;
     cpu::IdtEntry* lpIdt;

     static NO_ASAN void SetGdtEntry(std::size_t index, std::uint32_t base, std::uint32_t limit, std::uint8_t access,
                                     std::uint8_t granularity)
     {
          lpGdt[index].baseLow = base & 0xFFFF;
          lpGdt[index].baseMiddle = (base >> 16) & 0xFF;
          lpGdt[index].baseHigh = (base >> 24) & 0xFF;
          lpGdt[index].limitLow = limit & 0xFFFF;
          lpGdt[index].granularity = ((limit >> 16) & 0x0F) | (granularity & 0xF0);
          lpGdt[index].access = access;
     }

     static NO_ASAN void SetTssEntry(std::size_t index, std::uintptr_t tssAddress)
     {
          std::uint64_t base = tssAddress;
          std::uint32_t limit = sizeof(Tss64) - 1;

          lpGdt[index].limitLow = limit & 0xFFFF;
          lpGdt[index].baseLow = base & 0xFFFF;
          lpGdt[index].baseMiddle = (base >> 16) & 0xFF;
          lpGdt[index].baseHigh = (base >> 24) & 0xFF;
          lpGdt[index].access = 0x89; // Present, TSS
          lpGdt[index].granularity = 0x00;

          GdtEntry* upper = &lpGdt[index + 1];
          std::memset(upper, 0, sizeof(GdtEntry));
          upper->limitLow = (base >> 32) & 0xFFFF;
          upper->baseLow = (base >> 48) & 0xFFFF;
     }

     static NO_ASAN void SetIdtEntry(std::size_t index, std::uintptr_t handler, std::uint16_t selector,
                                     std::uint8_t ist, std::uint8_t typeAttr)
     {
          lpIdt[index].offsetLow = handler & 0xFFFF;
          lpIdt[index].offsetMiddle = (handler >> 16) & 0xFFFF;
          lpIdt[index].offsetHigh = (handler >> 32) & 0xFFFFFFFF;
          lpIdt[index].selector = selector;
          lpIdt[index].ist = ist;
          lpIdt[index].typeAttr = typeAttr;
          lpIdt[index].reserved = 0;
          _mm_clflush(&lpIdt[index]);
     }

     using InterruptRoutine = void (*)();

     extern "C" NO_ASAN InterruptRoutine KiInterruptVectorTable[256];
     extern "C" NO_ASAN void LoadTaskRegister(std::uint16_t selector);
     void KeProtect(void (*protectionFunction)(std::uintptr_t address, bool isCode))
     {
          if (protectionFunction == nullptr) return;
          protectionFunction(reinterpret_cast<std::uintptr_t>(lpGdt), false);
          protectionFunction(reinterpret_cast<std::uintptr_t>(lpIdt), false);
          protectionFunction(reinterpret_cast<std::uintptr_t>(KiInterruptVectorTable), false);
          for (auto i : KiInterruptVectorTable)
               protectionFunction(reinterpret_cast<std::uintptr_t>(i), true);
     }

     NO_ASAN void Initialise(std::uintptr_t (*lpPages)[2])
     {
          operations::DisableInterrupts();

          if (lpPages != nullptr)
          {
               auto& pages = *lpPages;
               const auto gdtPage = pages[0];
               auto tssPage = gdtPage + sizeof(cpu::GdtEntry[8]);
               auto idtPage = pages[1];

               lpGdt = new (reinterpret_cast<void*>(gdtPage + 0xffff'8000'0000'0000)) cpu::GdtEntry[8];
               lpTss = new (reinterpret_cast<void*>(tssPage + 0xffff'8000'0000'0000)) cpu::Tss64;
               lpIdt = new (reinterpret_cast<void*>(idtPage + 0xffff'8000'0000'0000)) cpu::IdtEntry[256];

               std::memset(lpGdt, 0, sizeof(cpu::GdtEntry[8]));
               std::memset(lpTss, 0, sizeof(cpu::Tss64));
               std::memset(lpIdt, 0, sizeof(cpu::IdtEntry[256]));

               SetGdtEntry(0, 0, 0, 0, 0);             // Null descriptor
               SetGdtEntry(1, 0, 0xFFFFF, 0x9A, 0xA0); // Kernel code (64-bit)
               SetGdtEntry(2, 0, 0xFFFFF, 0x92, 0xC0); // Kernel data (64-bit)
               SetGdtEntry(3, 0, 0xFFFFF, 0x9A, 0xA0); // Kernel code (32-bit)
               SetGdtEntry(4, 0, 0xFFFFF, 0x92, 0xC0); // Kernel data (32-bit)
               SetGdtEntry(5, 0, 0xFFFFF, 0xFA, 0xA0); // User code (64-bit)
               SetGdtEntry(6, 0, 0xFFFFF, 0xF2, 0xC0); // User data

               lpTss->iomapBase = sizeof(Tss64);
               SetTssEntry(7, reinterpret_cast<std::uintptr_t>(lpTss));
          }

          Gdtr gdtr{.limit = sizeof(cpu::GdtEntry[8]) - 1, .base = reinterpret_cast<std::uintptr_t>(lpGdt)};

#ifdef COMPILER_MSVC
          _lgdt(&gdtr);
#else
          asm volatile("lgdt %0" : : "m"(gdtr));
#endif

          if (lpPages != nullptr)
               for (std::size_t i = 0; i < 256; ++i)
                    SetIdtEntry(i, reinterpret_cast<std::uintptr_t>(KiInterruptVectorTable[i]), 0x08, 0, 0x8E);

          Idtr idtr{.limit = sizeof(cpu::IdtEntry[256]) - 1, .base = reinterpret_cast<std::uintptr_t>(lpIdt)};

#ifdef COMPILER_MSVC
          __lidt(&idtr);
#else
          asm volatile("lidt %0" : : "m"(idtr));
#endif

#ifdef COMPILER_MSVC
          ReloadCSCode();
#else
          asm volatile("pushq $0x08\n"
                       "lea 1f(%%rip), %%rax\n"
                       "push %%rax\n"
                       "lretq\n"
                       "1:\n"
                       :
                       :
                       : "rax");

          asm volatile("mov $0x10, %%ax\n"
                       "mov %%ax, %%ds\n"
                       "mov %%ax, %%es\n"
                       "mov %%ax, %%ss\n"
                       "mov %%ax, %%fs\n"
                       "mov %%ax, %%gs\n"
                       :
                       :
                       : "ax");
#endif
     }
} // namespace cpu

#elifdef ARCH_X8632 // ^^^ x86-64 / x86-32 vvv

namespace cpu
{
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
#pragma pack(pop)

#pragma pack(push, 1)
     struct Tss32
     {
          std::uint16_t link;
          std::uint16_t reserved0;
          std::uint32_t esp0;
          std::uint16_t ss0;
          std::uint16_t reserved1;
          std::uint32_t esp1;
          std::uint16_t ss1;
          std::uint16_t reserved2;
          std::uint32_t esp2;
          std::uint16_t ss2;
          std::uint16_t reserved3;
          std::uint32_t cr3;
          std::uint32_t eip;
          std::uint32_t eflags;
          std::uint32_t eax;
          std::uint32_t ecx;
          std::uint32_t edx;
          std::uint32_t ebx;
          std::uint32_t esp;
          std::uint32_t ebp;
          std::uint32_t esi;
          std::uint32_t edi;
          std::uint16_t es;
          std::uint16_t reserved4;
          std::uint16_t cs;
          std::uint16_t reserved5;
          std::uint16_t ss;
          std::uint16_t reserved6;
          std::uint16_t ds;
          std::uint16_t reserved7;
          std::uint16_t fs;
          std::uint16_t reserved8;
          std::uint16_t gs;
          std::uint16_t reserved9;
          std::uint16_t ldtr;
          std::uint16_t reserved10;
          std::uint16_t trap;
          std::uint16_t iomapBase;
     };
#pragma pack(pop)

#pragma pack(push, 1)
     struct IdtEntry
     {
          std::uint16_t offsetLow;
          std::uint16_t selector;
          std::uint8_t reserved;
          std::uint8_t typeAttr;
          std::uint16_t offsetHigh;
     };
#pragma pack(pop)

#pragma pack(push, 1)
     struct Gdtr
     {
          std::uint16_t limit;
          std::uint32_t base;
     };
#pragma pack(pop)

#pragma pack(push, 1)
     struct Idtr
     {
          std::uint16_t limit;
          std::uint32_t base;
     };
#pragma pack(pop)
     static GdtEntry sGdt[8];
     static Tss32 sTss;
     static IdtEntry sIdt[256];

     static void SetGdtEntry(std::size_t index, std::uint32_t base, std::uint32_t limit, std::uint8_t access,
                             std::uint8_t granularity)
     {
          sGdt[index].baseLow = base & 0xFFFF;
          sGdt[index].baseMiddle = (base >> 16) & 0xFF;
          sGdt[index].baseHigh = (base >> 24) & 0xFF;
          sGdt[index].limitLow = limit & 0xFFFF;
          sGdt[index].granularity = ((limit >> 16) & 0x0F) | (granularity & 0xF0);
          sGdt[index].access = access;
     }

     static void SetTssEntry(std::size_t index, std::uint32_t tssAddress)
     {
          std::uint32_t limit = sizeof(Tss32) - 1;
          SetGdtEntry(index, tssAddress, limit, 0x89, 0x00); // Present, TSS
     }

     static void SetIdtEntry(std::size_t index, std::uint32_t handler, std::uint16_t selector, std::uint8_t typeAttr)
     {
          sIdt[index].offsetLow = handler & 0xFFFF;
          sIdt[index].offsetHigh = (handler >> 16) & 0xFFFF;
          sIdt[index].selector = selector;
          sIdt[index].reserved = 0;
          sIdt[index].typeAttr = typeAttr;
     }

     extern "C" void LoadTaskRegister(std::uint16_t selector);

     void Initialise()
     {
          std::memset(sGdt, 0, sizeof(sGdt));
          std::memset(&sTss, 0, sizeof(sTss));
          std::memset(sIdt, 0, sizeof(sIdt));

          SetGdtEntry(0, 0, 0, 0, 0);             // Null descriptor
          SetGdtEntry(1, 0, 0xFFFFF, 0x9A, 0xCF); // Kernel code (32-bit)
          SetGdtEntry(2, 0, 0xFFFFF, 0x92, 0xCF); // Kernel data
          SetGdtEntry(3, 0, 0xFFFFF, 0xFA, 0xCF); // User code (32-bit)
          SetGdtEntry(4, 0, 0xFFFFF, 0xF2, 0xCF); // User data

          sTss.iomapBase = sizeof(Tss32);
          sTss.ss0 = 0x10;
          SetTssEntry(5, reinterpret_cast<std::uint32_t>(&sTss));

          Gdtr gdtr;
          gdtr.limit = sizeof(sGdt) - 1;
          gdtr.base = reinterpret_cast<std::uint32_t>(&sGdt);

#ifdef COMPILER_MSVC
          __lgdt(&gdtr);
#else
          asm volatile("lgdt %0" : : "m"(gdtr));
#endif

#ifdef COMPILER_MSVC
#else
          asm volatile("mov $0x10, %%ax\n"
                       "mov %%ax, %%ds\n"
                       "mov %%ax, %%es\n"
                       "mov %%ax, %%fs\n"
                       "mov %%ax, %%gs\n"
                       "mov %%ax, %%ss\n"
                       "ljmp $0x08, $1f\n"
                       "1:\n"
                       :
                       :
                       : "eax");
#endif

          for (std::size_t i = 0; i < 256; ++i)
               SetIdtEntry(i, reinterpret_cast<std::uint32_t>(DummyInterruptHandler), 0x08, 0x8E);

          Idtr idtr;
          idtr.limit = sizeof(sIdt) - 1;
          idtr.base = reinterpret_cast<std::uint32_t>(&sIdt);

#ifdef COMPILER_MSVC
          __lidt(&idtr);
#else
          asm volatile("lidt %0" : : "m"(idtr));
#endif

#ifdef COMPILER_MSVC
          LoadTaskRegister(0);
#else
          asm volatile("ltr %w0" : : "r"(static_cast<std::uint16_t>(0)));
#endif
     }
} // namespace cpu

#elifdef ARCH_ARM64 // ^^^ x86-32 / ARM64 vvv

namespace cpu
{
     extern "C" void* ExceptionVectorTable[];

     void Initialise()
     {
          std::uint64_t vbar = reinterpret_cast<std::uint64_t>(&ExceptionVectorTable);

#ifdef COMPILER_MSVC
          ::_WriteStatusReg(ARM64_SYSREG(3, 0, 12, 0, 0), vbar);
#else
          asm volatile("msr vbar_el1, %0" : : "r"(vbar));
#endif

          std::uint64_t cpacr = 0x3 << 20;
#ifdef COMPILER_MSVC
          ::_WriteStatusReg(ARM64_SYSREG(3, 0, 12, 0, 0), vbar);     // VBAR_EL1
          ::_WriteStatusReg(ARM64_SYSREG(3, 0, 1, 0, 2), 0x3 << 20); // CPACR_EL1
          ::__isb(_ARM64_BARRIER_SY);
#else
          asm volatile("msr vbar_el1, %0" : : "r"(vbar));
          asm volatile("msr cpacr_el1, %0" : : "r"(0x3uz << 20));
          asm volatile("isb");
#endif
     }
} // namespace cpu

#else // ^^^ ARM64
#error Invalid ISA
#endif
