#pragma once

#include <cstddef>
#include <cstdint>

#if defined(__clang__) || defined(__GNUC__)
#define NO_ASAN __attribute__((no_sanitize("address")))
#else
#define NO_ASAN
#endif

// XMM_SAVE_AREA32
struct XMM_SAVE_AREA32
{
     std::uint16_t controlWord;
     std::uint16_t statusWord;
     std::uint16_t tagWord;
     std::uint16_t reserved1;
     std::uint32_t errorOpcode;
     std::uint32_t errorOffset;
     std::uint16_t errorSelector;
     std::uint16_t reserved2;
     std::uint32_t dataOffset;
     std::uint16_t dataSelector;
     std::uint16_t reserved3;
     std::uint32_t mxCsr;
     std::uint32_t mxCsr_Mask;
     std::uint8_t floatRegisters[8][10];
     std::uint8_t xmmRegisters[16][16];
     std::uint8_t reserved4[96];
};
struct NEON128
{
     std::uint8_t bytes[16];
};
struct M128A
{
     std::uint64_t low;
     std::uint64_t high;
};
struct CONTEXT
{
     std::uint64_t P1Home;
     std::uint64_t P2Home;
     std::uint64_t P3Home;
     std::uint64_t P4Home;
     std::uint64_t P5Home;
     std::uint64_t P6Home;
     std::uint32_t ContextFlags;
     std::uint32_t MxCsr;
     std::uint16_t SegCs;
     std::uint16_t SegDs;
     std::uint16_t SegEs;
     std::uint16_t SegFs;
     std::uint16_t SegGs;
     std::uint16_t SegSs;
     std::uint32_t EFlags;
     std::uint64_t Dr0;
     std::uint64_t Dr1;
     std::uint64_t Dr2;
     std::uint64_t Dr3;
     std::uint64_t Dr6;
     std::uint64_t Dr7;
     std::uint64_t Rax;
     std::uint64_t Rcx;
     std::uint64_t Rdx;
     std::uint64_t Rbx;
     std::uint64_t Rsp;
     std::uint64_t Rbp;
     std::uint64_t Rsi;
     std::uint64_t Rdi;
     std::uint64_t R8;
     std::uint64_t R9;
     std::uint64_t R10;
     std::uint64_t R11;
     std::uint64_t R12;
     std::uint64_t R13;
     std::uint64_t R14;
     std::uint64_t R15;
     std::uint64_t Rip;
     union
     {
          XMM_SAVE_AREA32 FltSave;
          NEON128 Q[16];
          std::uint64_t D[32];
          struct
          {
               M128A Header[2];
               M128A Legacy[8];
               M128A Xmm0;
               M128A Xmm1;
               M128A Xmm2;
               M128A Xmm3;
               M128A Xmm4;
               M128A Xmm5;
               M128A Xmm6;
               M128A Xmm7;
               M128A Xmm8;
               M128A Xmm9;
               M128A Xmm10;
               M128A Xmm11;
               M128A Xmm12;
               M128A Xmm13;
               M128A Xmm14;
               M128A Xmm15;
          } DUMMYSTRUCTNAME;
          std::uint32_t S[32];
     } DUMMYUNIONNAME;
     M128A VectorRegister[26];
     std::uint64_t VectorControl;
     std::uint64_t DebugControl;
     std::uint64_t LastBranchToRip;
     std::uint64_t LastBranchFromRip;
     std::uint64_t LastExceptionToRip;
     std::uint64_t LastExceptionFromRip;
};

extern "C" void KeCaptureContext(CONTEXT* context);

namespace operations
{
     void DisableInterrupts();
     void EnableInterrupts();
     void Yield();
     std::uint64_t ReadCurrentCycles();
     void Halt();
     bool SupportsVMX();

     void InitialiseSerial();
     void WriteSerialCharacter(char value);
     char ReadSerialCharacter();
     char TryReadSerialCharacter();
     void SerialPushCharacter(char c);
     void SerialHoldLineHigh();

     std::uint8_t ReadPort8(std::uint16_t port);
     std::uint16_t ReadPort16(std::uint16_t port);
     std::uint32_t ReadPort32(std::uint16_t port);

     void WritePort8(std::uint16_t port, std::uint8_t value);
     void WritePort16(std::uint16_t port, std::uint16_t value);
     void WritePort32(std::uint16_t port, std::uint32_t value);

     template <int = 0> void WriteSerialString(const char* value)
     {
          while (value[0] != 0) WriteSerialCharacter(*value++);
     }
     template <int = 0> void WriteSerialString(const char* value, std::size_t nCharacters)
     {
          for (std::size_t i = 0; i < nCharacters; i++) WriteSerialCharacter(value[i]);
     }
     template <int = 0> void WriteSerialString(const char8_t* value, std::size_t nCharacters)
     {
          for (std::size_t i = 0; i < nCharacters; i++) WriteSerialCharacter(static_cast<char>(value[i]));
     }
     template <std::size_t N> void WriteSerialString(const char8_t (&string)[N]) // NOLINT
     {
          for (std::size_t i = 0; i < N; i++) WriteSerialCharacter(static_cast<char>(string[i]));
     }
} // namespace operations
