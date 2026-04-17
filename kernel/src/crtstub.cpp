#include <cstdint>
#include "process/taskScheduler.h"
#include "utils/kdbg.h"
extern "C" int _CrtDbgReport(int reportType, // NOLINT(readability-identifier-naming)
                             const char* filename, int linenumber, const char* moduleName, const char* format, ...)
{
     (void)reportType;
     (void)filename;
     (void)linenumber;
     (void)moduleName;
     (void)format;
     return 0;
}

extern "C" float __cdecl ceilf(const float x) // NOLINT
{
     const auto i = static_cast<int>(x);
     if (static_cast<float>(i) == x) return x;
     if (x > 0.0f) return static_cast<float>(i + 1);
     return static_cast<float>(i);
}

__declspec(dllexport) __attribute__((no_stack_protector)) extern "C" std::uint64_t __chkstk() // NOLINT
{
     unsigned __int64 result; // rax
     char* v1;                // r10
     char* StackLimit;        // r11
     char v3;                 // [rsp+18h] [rbp+8h] BYREF

     v1 = &v3 - result;
     if ((unsigned __int64)&v3 < result) v1 = 0;
     StackLimit = (char*)process::KeCurrentThread()->stackBase + process::KeCurrentThread()->stackSize;
     if (v1 < StackLimit)
     {
          //LOWORD(v1) = (unsigned __int16)v1 & 0xF000
          v1 = (char*)((unsigned __int64)v1 & 0xFFFFFFFFFFFF0000);
          do StackLimit -= 4096;
          while (v1 < StackLimit);
     }
     return result;
}

extern "C" std::uintptr_t __CxxFrameHandler4(void*, void*, void*, void*) noexcept
{
     debugging::DbgWrite(u8"*** Unhandled C++ exception - system is likely unstable! ***\r\n");
     operations::DisableInterrupts();
     while (true) operations::Halt();
}

extern "C" const void* memchr(const void* ptr, int value, std::size_t count) noexcept
{
     const auto* p = static_cast<const unsigned char*>(ptr);
     const auto v = static_cast<unsigned char>(value);
     for (std::size_t i = 0; i < count; ++i)
          if (p[i] == v) return p + i;
     return nullptr;
}
unsigned int KiRotateLeft32(unsigned int value, int shift) noexcept
{
     const auto s = static_cast<unsigned int>(shift) & 31u;
     return (value << s) | (value >> (32u - s));
}
namespace std
{
     [[noreturn]] void _Xlength_error(const char*) noexcept
     {
          operations::DisableInterrupts();
          while (true) operations::Halt();
     }

     [[noreturn]] void _Xout_of_range(const char*) noexcept
     {
          operations::DisableInterrupts();
          while (true) operations::Halt();
     }
} // namespace std

namespace std
{
     void (*_Raise_handler)(const stdext::exception&) = nullptr;
}

const void* __stdcall __std_find_trivial_1(const void* _First, const void* _Last, std::uint8_t _Val) noexcept
{
     const auto* first = static_cast<const std::uint8_t*>(_First);
     const auto* last = static_cast<const std::uint8_t*>(_Last);
     const auto value = _Val;
     for (; first != last; ++first)
          if (*first == value) return first;
     return last;
}

extern "C" int atexit(void (*function)()) // NOLINT
{
     (void)function;
     return 0;
}
