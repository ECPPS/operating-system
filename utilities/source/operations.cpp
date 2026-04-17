#include <utils/identify.h>
#include <utils/operations.h>
#include <array>
#include <atomic>
#include "utils/kdbg.h"

#if defined(ARCH_ARM64) && defined(COMPILER_MSVC)
#include <intrin.h>
#endif

#ifdef ARCH_X8664 // x86-64 vvv

namespace
{
     constexpr std::uint16_t COM1_PORT = 0x3F8;
     constexpr std::size_t SERIAL_BUFFER_SIZE = 1024;

     std::array<char, SERIAL_BUFFER_SIZE> TxBuffer{};
     std::array<char, SERIAL_BUFFER_SIZE> RxBuffer{};

     std::atomic<std::size_t> TxHead{};
     std::atomic<std::size_t> TxTail{};

     std::atomic<std::size_t> RxHead{};
     std::atomic<std::size_t> RxTail{};

     std::atomic<bool> g_serialInitialised{false};

#ifdef COMPILER_MSVC
     NO_ASAN inline std::uint8_t In8(std::uint16_t port) { return static_cast<std::uint8_t>(__inbyte(port)); }
     NO_ASAN inline std::uint16_t In16(std::uint16_t port) { return static_cast<std::uint16_t>(__inword(port)); }
     NO_ASAN inline std::uint32_t In32(std::uint16_t port) { return static_cast<std::uint32_t>(__indword(port)); }
     NO_ASAN inline void Out8(std::uint16_t port, std::uint8_t value) { __outbyte(port, value); }
     NO_ASAN inline void Out16(std::uint16_t port, std::uint16_t value) { __outword(port, value); }
     NO_ASAN inline void Out32(std::uint16_t port, std::uint32_t value)
     {
          __outdword(port, value);
#elifdef COMPILER_CLANG
     NO_ASAN inline std::uint8_t In8(std::uint16_t port)
     {
          std::uint8_t value{};
          asm volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
          return value;
     }
     NO_ASAN inline std::uint16_t In16(std::uint16_t port)
     {
          std::uint16_t value{};
          asm volatile("inw %1, %0" : "=a"(value) : "Nd"(port));
          return value;
     }
     NO_ASAN inline std::uint32_t In32(std::uint16_t port)
     {
          std::uint32_t value{};
          asm volatile("inl %1, %0" : "=a"(value) : "Nd"(port));
          return value;
     }
     NO_ASAN inline void Out8(std::uint16_t port, std::uint8_t value)
     { asm volatile("outb %0, %1" : : "a"(value), "Nd"(port)); }
     NO_ASAN inline void Out16(std::uint16_t port, std::uint16_t value)
     { asm volatile("outw %0, %1" : : "a"(value), "Nd"(port)); }
     NO_ASAN inline void Out32(std::uint16_t port, std::uint32_t value)
     { asm volatile("outl %0, %1" : : "a"(value), "Nd"(port)); }
#endif
     } // namespace

     NO_ASAN void operations::InitialiseSerial()
     {
          debugging::DbgWrite(u8"Initialising serial port COM1\r\n");

          Out8(COM1_PORT + 1, 0x00); // disable interrupts
          Out8(COM1_PORT + 3, 0x80); // enable DLAB
          Out8(COM1_PORT + 0, 0x03); // baud rate divisor low byte (38400)
          Out8(COM1_PORT + 1, 0x00); // baud rate divisor high byte
          Out8(COM1_PORT + 3, 0x03); // 8 bits, no parity, one stop bit
          Out8(COM1_PORT + 2, 0xC7); // FIFO enabled, clear TX/RX, 14-byte threshold
          Out8(COM1_PORT + 4, 0x0B); // IRQs enabled, RTS/DSR set
          Out8(COM1_PORT + 1, 0x03); // enable interrupts: RX and TX

          g_serialInitialised.store(true, std::memory_order_release);
     }

     NO_ASAN void PushRxChar(char c)
     {
          const auto head = RxHead.load(std::memory_order_relaxed);
          const auto next = (head + 1) % SERIAL_BUFFER_SIZE;
          if (next != RxTail.load(std::memory_order_acquire))
          {
               RxBuffer[head] = c;
               RxHead.store(next, std::memory_order_release);
          }
     }

     NO_ASAN void PushTxChar(char c)
     {
          const auto head = TxHead.load(std::memory_order_relaxed);
          const auto next = (head + 1) % SERIAL_BUFFER_SIZE;
          if (next != TxTail.load(std::memory_order_acquire))
          {
               TxBuffer[head] = c;
               TxHead.store(next, std::memory_order_release);
          }
     }

     NO_ASAN void operations::WriteSerialCharacter(char value)
     {
          while (!(In8(COM1_PORT + 5) & 0x20)) {} // wait for TX ready
          Out8(COM1_PORT, static_cast<std::uint8_t>(value));
     }

     NO_ASAN char operations::ReadSerialCharacter()
     {
          while (RxTail.load(std::memory_order::acquire) == RxHead.load(std::memory_order::acquire)) Halt();

          const auto tail = RxTail.load(std::memory_order::relaxed);
          const char c = RxBuffer[tail];
          RxTail.store((tail + 1) % SERIAL_BUFFER_SIZE, std::memory_order::release);
          return c;
     }
     std::uint8_t operations::ReadPort8(std::uint16_t port) { return In8(port); }
     std::uint16_t operations::ReadPort16(std::uint16_t port) { return In16(port); }
     std::uint32_t operations::ReadPort32(std::uint16_t port) { return In32(port); }

     void operations::WritePort8(std::uint16_t port, std::uint8_t value) { Out8(port, value); }
     void operations::WritePort16(std::uint16_t port, std::uint16_t value) { Out16(port, value); }
     void operations::WritePort32(std::uint16_t port, std::uint32_t value) { Out32(port, value); }

     void operations::SerialPushCharacter(char c) { PushRxChar(c); }
     void operations::SerialHoldLineHigh()
     {
          while (true)
          {
               const auto tail = TxTail.load(std::memory_order_relaxed);
               if (tail == TxHead.load(std::memory_order_acquire)) break; // no more data

               while (!(In8(COM1_PORT + 5) & 0x20)) {} // wait for TX ready
               const char c = TxBuffer[tail];
               TxTail.store((tail + 1) % SERIAL_BUFFER_SIZE, std::memory_order_release);
               Out8(COM1_PORT, static_cast<std::uint8_t>(c));
          }
     }

#ifdef COMPILER_MSVC
#include <intrin.h>

     NO_ASAN void operations::EnableInterrupts() { ::_enable(); }
     NO_ASAN void operations::DisableInterrupts() { ::_disable(); }
     NO_ASAN void operations::Yield() { ::_mm_pause(); }
     NO_ASAN std::uint64_t operations::ReadCurrentCycles() { return ::__rdtsc(); }
     NO_ASAN void operations::Halt() { ::__halt(); }
#elifdef COMPILER_CLANG // ^^^ MSVC / vvv clang
#include <x86intrin.h>

NO_ASAN void operations::EnableInterrupts() { asm volatile("sti"); }
NO_ASAN void operations::DisableInterrupts() { asm volatile("cli"); }
NO_ASAN void operations::Yield() { asm volatile("pause"); }
NO_ASAN std::uint64_t operations::ReadCurrentCycles()
{
     std::uint32_t low{};
     std::uint32_t high{};
     asm volatile("rdtsc" : "=a"(low), "=d"(high));
     return (static_cast<std::uint64_t>(high) << 32) | low;
}
NO_ASAN void operations::Halt() { asm volatile("hlt"); }
NO_ASAN bool operations::SupportsVMX()
{
     std::uint32_t eax{};
     std::uint32_t ebx{};
     std::uint32_t ecx{};
     std::uint32_t edx{};
     asm volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1));
     return (ecx & (1 << 5)) != 0;
}

#endif

#elifdef ARCH_X8632 // ^^^ x86-64 / x86-32 vvv
#ifdef COMPILER_MSVC
#include <intrin.h>

NO_ASAN void operations::EnableInterrupts() { ::_enable(); }
NO_ASAN void operations::DisableInterrupts() { ::_disable(); }
NO_ASAN void operations::Yield() { ::_mm_pause(); }
NO_ASAN std::uint64_t operations::ReadCurrentCycles() { return ::__rdtsc(); }
NO_ASAN void operations::Halt() { ::__halt(); }
#elifdef COMPILER_CLANG // ^^^ MSVC / vvv clang
#include <x86intrin.h>

NO_ASAN void operations::EnableInterrupts() { asm volatile("sti"); }
NO_ASAN void operations::DisableInterrupts() { asm volatile("cli"); }
NO_ASAN void operations::Yield() { asm volatile("pause"); }
NO_ASAN std::uint64_t operations::ReadCurrentCycles()
{
     std::uint32_t low{};
     std::uint32_t high{};
     asm volatile("rdtsc" : "=a"(low), "=d"(high));
     return (static_cast<std::uint64_t>(high) << 32uz) | low;
}
NO_ASAN void operations::Halt() { asm volatile("hlt"); }
#elifdef COMPILER_GCC // ^^^ clang / vvv gcc
#include <x86intrin.h>

NO_ASAN void operations::EnableInterrupts() { asm volatile("sti"); }
NO_ASAN void operations::DisableInterrupts() { asm volatile("cli"); }
NO_ASAN void operations::Yield() { asm volatile("pause"); }
NO_ASAN std::uint64_t operations::ReadCurrentCycles()
{
     std::uint32_t low{};
     std::uint32_t high{};
     asm volatile("rdtsc" : "=a"(low), "=d"(high));
     return (static_cast<std::uint64_t>(high) << 32) | low;
}
NO_ASAN void operations::Halt() { asm volatile("hlt"); }
#endif

#elifdef ARCH_ARM64 // ^^^ x86-32 / ARM vvv

namespace
{
     constexpr uintptr_t UART0_BASE = 0x09000000;

     struct UARTRegisters
     {
          volatile std::uint32_t dr;
          volatile std::uint32_t rsrEcr;
          std::uint32_t reserveD0[4]; // NOLINT
          volatile std::uint32_t fr;
          std::uint32_t reserveD1;
          volatile std::uint32_t ilpr;
          volatile std::uint32_t ibrd;
          volatile std::uint32_t fbrd;
          volatile std::uint32_t lcrh;
          volatile std::uint32_t cr;
          volatile std::uint32_t ifls;
          volatile std::uint32_t imsc;
          volatile std::uint32_t ris;
          volatile std::uint32_t mis;
          volatile std::uint32_t icr;
     };
} // namespace
void operations::InitialiseSerial() {}

void operations::WriteSerialCharacter(char value)
{
     auto* uart = reinterpret_cast<UARTRegisters*>(UART0_BASE + 0xffff'8000'0000'0000);

#ifdef COMPILER_MSVC    // MSVC vvv
     while (uart->fr & (1 << 5)) Yield();
     uart->dr = static_cast<std::uint32_t>(value);
#elifdef COMPILER_CLANG // ^^^ MSVC / Clang vvv
     while (uart->fr & (1u << 5)) Yield();
     uart->dr = static_cast<std::uint32_t>(static_cast<unsigned char>(value));
#endif                  // ^^^ Clang
}
char operations::ReadSerialCharacter()
{
     volatile auto* uart = reinterpret_cast<volatile UARTRegisters*>(0x09000000uz + 0xffff'8000'0000'0000uz);

     while (uart->fr & (1u << 4)) Yield();
     char c = static_cast<char>(uart->dr & 0xff);

     return c;
}
char operations::TryReadSerialCharacter()
{
     auto* uart = reinterpret_cast<UARTRegisters*>(UART0_BASE + 0xffff'8000'0000'0000);

     if (uart->fr & (1u << 4)) return 0;
     return static_cast<char>(uart->dr & 0xFF);
}

#ifdef COMPILER_MSVC
#include <intrin.h>

NO_ASAN void operations::EnableInterrupts() { ::_enable(); }
NO_ASAN void operations::DisableInterrupts() { ::_disable(); }
NO_ASAN void operations::Yield() { ::__yield(); }
NO_ASAN std::uint64_t operations::ReadCurrentCycles()
{
     std::uint64_t cycles = 0;
     cycles = ::_ReadStatusReg(ARM64_SYSREG(3, 3, 14, 0, 0));
     return cycles;
}
NO_ASAN void operations::Halt() { ::__wfi(); }
#elifdef COMPILER_CLANG // ^^^ MSVC / vvv clang
NO_ASAN void operations::EnableInterrupts() { asm volatile("msr daifclr, #2"); }
NO_ASAN void operations::DisableInterrupts() { asm volatile("msr daifset, #2"); }
NO_ASAN void operations::Yield() { asm volatile("yield"); }
NO_ASAN std::uint64_t operations::ReadCurrentCycles()
{
     std::uint64_t cycles{};
     asm volatile("mrs %0, pmccntr_el0" : "=r"(cycles));
     return cycles;
}
NO_ASAN void operations::Halt() { asm volatile("wfi"); }
#elifdef COMPILER_GCC   // ^^^ clang / vvv gcc
NO_ASAN void operations::EnableInterrupts() { asm volatile("msr daifclr, #2"); }
NO_ASAN void operations::DisableInterrupts() { asm volatile("msr daifset, #2"); }
NO_ASAN void operations::Yield() { asm volatile("yield"); }
NO_ASAN std::uint64_t operations::ReadCurrentCycles()
{
     std::uint64_t cycles{};
     asm volatile("mrs %0, pmccntr_el0" : "=r"(cycles));
     return cycles;
}
NO_ASAN void operations::Halt() { asm volatile("wfi"); }
#endif

NO_ASAN std::uint8_t operations::ReadPort8(std::uint16_t port)
{
#ifdef COMPILER_MSVC
#else
#endif
}
NO_ASAN std::uint16_t operations::ReadPort16(std::uint16_t port)
{
#ifdef COMPILER_MSVC
#else
#endif
}
NO_ASAN std::uint32_t operations::ReadPort32(std::uint16_t port)
{
#ifdef COMPILER_MSVC
#else
#endif
}

NO_ASAN void operations::WritePort8(std::uint16_t port, std::uint8_t value)
{
#ifdef COMPILER_MSVC
#else
#endif
}
NO_ASAN void operations::WritePort16(std::uint16_t port, std::uint16_t value)
{
#ifdef COMPILER_MSVC
#else
#endif
}
NO_ASAN void operations::WritePort32(std::uint16_t port, std::uint32_t value)
{
#ifdef COMPILER_MSVC
#else
#endif
}

#else // ^^^ ARM
#error Invalid ISA
#endif
