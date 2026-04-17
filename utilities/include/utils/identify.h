#pragma once

#include <cstddef>
#include <string_view>

#ifdef __clang__
#define ACOMPILER "clang"
#define WCOMPILER L"clang"
#define COMPILER_CLANG
#elifdef _MSC_VER
#define ACOMPILER "msvc"
#define WCOMPILER L"msvc"
#define COMPILER_MSVC
#elifdef __GNUC__
#define ACOMPILER "gcc"
#define WCOMPILER L"gcc"
#define COMPILER_GCC
#else
#define ACOMPILER "unknown"
#define WCOMPILER L"unknown"
#endif

#if defined(_M_X64) || defined(__x86_64__)
#define AARCHITECTURE "x86-64"
#define WARCHITECTURE L"x86-64"
#define ARCH_X8664
#elifdef _M_IX86
#define AARCHITECTURE "x86-32"
#define WARCHITECTURE L"x86-32"
#define ARCH_X8632
#elifdef __i386__
#define AARCHITECTURE "x86-32"
#define WARCHITECTURE L"x86-32"
#define ARCH_X8632
#elifdef _M_ARM64
#define AARCHITECTURE "ARM64"
#define WARCHITECTURE L"ARM64"
#define ARCH_ARM64
#elifdef __aarch64__
#define AARCHITECTURE "ARM64"
#define WARCHITECTURE L"ARM64"
#define ARCH_ARM64
#else
#define AARCHITECTURE "unknown"
#define WARCHITECTURE L"unknown"
#endif

#ifdef COMPILER_MSVC
#define DLLEXPORT __declspec(dllexport)
#define DLLIMPORT __declspec(dllimport)
#elifdef COMPILER_CLANG
#define DLLEXPORT __declspec(dllexport)
#define DLLIMPORT __declspec(dllimport)
#endif

constexpr auto AConfiguration = std::string_view{ACOMPILER " " AARCHITECTURE};
constexpr auto WConfiguration = std::wstring_view{WCOMPILER L" " WARCHITECTURE};

constexpr std::size_t OsVersionMajor = 0;
constexpr std::size_t OsVersionMinor = 1;

// Kernel virtual address base (higher half)
#if defined(ARCH_X8664)
constexpr std::uintptr_t KernelVirtualBase = 0xFFFFFFFF80000000ULL;
constexpr std::uintptr_t KernelStackVirtualBase = 0xFFFFFFFF7FC00000ULL;
constexpr std::uintptr_t VideoVirtualBase = 0xFFFFFFFF81000000ULL;
#elif defined(ARCH_X8632)
constexpr std::uintptr_t KernelVirtualBase = 0xC0000000UL;
constexpr std::uintptr_t KernelStackVirtualBase = 0xBFC00000UL;
#elif defined(ARCH_ARM64)
constexpr std::uintptr_t KernelVirtualBase = 0xFFFFFF8000000000ULL;
constexpr std::uintptr_t VideoVirtualBase = 0xFFFFFF8100000000ULL;
constexpr std::uintptr_t KernelStackVirtualBase = 0xFFFFFF7FC0000000ULL;
#else
#error "Unsupported architecture"
#endif

constexpr std::size_t KernelStackSize = 1024ULL * 1024ULL * 2uz; // 2MiB
