#include "kasan.h"
#include <utils/operations.h>
#include <cstdint>
#include <cstring>
#include <memory>
#include "../kinit.h"
#include "utils/kdbg.h"

#define PRINT_SELF

static void* g_shadowMemory{};
extern "C" std::uintptr_t __asan_shadow_memory_dynamic_address = 0xDFFFA00100000000;
extern "C" bool __asan_option_detect_stack_use_after_return = false;
extern const bool _Asan_string_should_annotate = false;

static NO_ASAN void* CommitShadowPage(std::uintptr_t offset)
{
     std::uintptr_t pageAddr = reinterpret_cast<std::uintptr_t>(g_shadowMemory) + (offset & ~0xFFF);
     auto* node = g_kernelProcess->QueryAddress(pageAddr);
     if (!node || node->entry.state != memory::VADMemoryState::Committed)
     {
          g_kernelProcess->AllocateVirtualMemory(reinterpret_cast<void*>(pageAddr), 0x1000,
                                                 memory::AllocationFlags::Commit |
                                                     memory::AllocationFlags::ImmediatePhysical,
                                                 memory::MemoryProtection::ReadWrite);
          std::memset(reinterpret_cast<void*>(pageAddr), 0xF1, 0x1000);
     }
     return reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(g_shadowMemory) + offset);
}

static NO_ASAN void* GetShadowAddress(void* ptr, std::size_t size)
{
     if (!g_shadowMemory) return nullptr;
     auto addr = reinterpret_cast<std::uintptr_t>(ptr);
     addr -= 0xffff'8000'0000'0000;

     std::uintptr_t offsetStart = addr >> 3;
     std::uintptr_t offsetEnd = (addr + size - 1) >> 3;

     for (std::uintptr_t page = offsetStart & ~0xFFF; page <= offsetEnd; page += 0x1000) CommitShadowPage(page);

     return reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(g_shadowMemory) + offsetStart);
}

static NO_ASAN void PoisonMemory(void* ptr, std::size_t size, std::uint8_t poison)
{
     auto* shadow = reinterpret_cast<std::uint8_t*>(GetShadowAddress(ptr, size));
     if (!shadow)
     {
          PRINT_SELF;
          return;
     }
     std::uninitialized_fill_n(shadow, size >> 3, poison);
}

static NO_ASAN void UnpoisonMemory(void* ptr, std::size_t size)
{
     auto* shadow = reinterpret_cast<std::uint8_t*>(GetShadowAddress(ptr, size));
     if (!shadow) return;
     std::uninitialized_fill_n(shadow, size >> 3, 0);
}

void NO_ASAN KASANAllocateHeap(void* base, std::size_t size)
{
     if (!g_shadowMemory) return;
     UnpoisonMemory(base, size);
     PoisonMemory(reinterpret_cast<std::uint8_t*>(base) - 16, 16, 0xF1);
     PoisonMemory(reinterpret_cast<std::uint8_t*>(base) + size, 16, 0xF1);
}

void NO_ASAN KASANFreeHeap(void* base, std::size_t size)
{
     if (!g_shadowMemory) return;
     PoisonMemory(base, size, 0xF2);
}

#define ASAN_STUB extern "C" NO_ASAN

ASAN_STUB void __sanitizer_annotate_contiguous_container(const void* _First, const void* _End, const void* _Old_last,
                                                         const void* _New_last)
{
}

// version
ASAN_STUB bool __asan_version_mismatch_check_v8() { return true; }

// init
ASAN_STUB void __asan_init() { g_shadowMemory = nullptr; }
ASAN_STUB void __asan_before_dynamic_init() { PRINT_SELF; }
ASAN_STUB void __asan_after_dynamic_init() { PRINT_SELF; }

// return
ASAN_STUB void __asan_handle_no_return() { PRINT_SELF; }

// set shadow
ASAN_STUB void __asan_set_shadow_00(std::uintptr_t addr, std::size_t size, std::uint8_t value) { PRINT_SELF; }
ASAN_STUB void __asan_set_shadow_f5(std::uintptr_t addr, std::size_t size, std::uint8_t value) { PRINT_SELF; }
ASAN_STUB void __asan_set_shadow_f8(std::uintptr_t addr, std::size_t size, std::uint8_t value) { PRINT_SELF; }
ASAN_STUB void __asan_set_shadow_f1(std::uintptr_t addr, std::size_t size, std::uint8_t value) { PRINT_SELF; }
ASAN_STUB void __asan_set_shadow_f2(std::uintptr_t addr, std::size_t size, std::uint8_t value) { PRINT_SELF; }
ASAN_STUB void __asan_set_shadow_f3(std::uintptr_t addr, std::size_t size, std::uint8_t value) { PRINT_SELF; }
ASAN_STUB void __asan_set_shadow_f4(std::uintptr_t addr, std::size_t size, std::uint8_t value) { PRINT_SELF; }

template <std::size_t N> NO_ASAN void __asan_load(std::uintptr_t addr)
{
     if (!g_shadowMemory) return;
     // TODO: implement
}

template <std::size_t N> NO_ASAN void __asan_store(std::uintptr_t addr)
{
     if (!g_shadowMemory) return;
     // TODO: implement
}

ASAN_STUB void __asan_loadN(std::uintptr_t addr, std::size_t size) {}

ASAN_STUB void __asan_storeN(std::uintptr_t addr, std::size_t size) {}

// load
ASAN_STUB void __asan_load1(std::uintptr_t addr) { __asan_load<1>(addr); }
ASAN_STUB void __asan_load2(std::uintptr_t addr) { __asan_load<2>(addr); }
ASAN_STUB void __asan_load4(std::uintptr_t addr) { __asan_load<4>(addr); }
ASAN_STUB void __asan_load8(std::uintptr_t addr) { __asan_load<8>(addr); }
ASAN_STUB void __asan_load16(std::uintptr_t addr) { __asan_load<16>(addr); }

// store
ASAN_STUB void __asan_store1(std::uintptr_t addr) { __asan_store<1>(addr); }

ASAN_STUB void __asan_store2(std::uintptr_t addr) { __asan_store<2>(addr); }

ASAN_STUB void __asan_store4(std::uintptr_t addr) { __asan_store<4>(addr); }

ASAN_STUB void __asan_store8(std::uintptr_t addr) { __asan_store<8>(addr); }

ASAN_STUB void __asan_store16(std::uintptr_t addr) { __asan_store<16>(addr); }

// load-report
ASAN_STUB void __asan_report_load1(std::uintptr_t addr) { PRINT_SELF; }
ASAN_STUB void __asan_report_load2(std::uintptr_t addr) { PRINT_SELF; }
ASAN_STUB void __asan_report_load4(std::uintptr_t addr) { PRINT_SELF; }
ASAN_STUB void __asan_report_load8(std::uintptr_t addr) { PRINT_SELF; }

// store-report
ASAN_STUB void __asan_report_store1(std::uintptr_t addr) { PRINT_SELF; }
ASAN_STUB void __asan_report_store2(std::uintptr_t addr) { PRINT_SELF; }
ASAN_STUB void __asan_report_store4(std::uintptr_t addr) { PRINT_SELF; }
ASAN_STUB void __asan_report_store8(std::uintptr_t addr) { PRINT_SELF; }

// stack alloc
ASAN_STUB void __asan_stack_malloc_0(std::uintptr_t addr, std::size_t size) { PRINT_SELF; }
ASAN_STUB void __asan_stack_malloc_1(std::uintptr_t addr, std::size_t size) { PRINT_SELF; }
ASAN_STUB void __asan_stack_malloc_2(std::uintptr_t addr, std::size_t size) { PRINT_SELF; }
ASAN_STUB void __asan_stack_malloc_3(std::uintptr_t addr, std::size_t size) { PRINT_SELF; }
ASAN_STUB void __asan_stack_malloc_4(std::uintptr_t addr, std::size_t size) { PRINT_SELF; }
ASAN_STUB void __asan_stack_malloc_5(std::uintptr_t addr, std::size_t size) { PRINT_SELF; }
ASAN_STUB void __asan_stack_malloc_6(std::uintptr_t addr, std::size_t size) { PRINT_SELF; }
ASAN_STUB void __asan_stack_malloc_7(std::uintptr_t addr, std::size_t size) { PRINT_SELF; }

// stack free
ASAN_STUB void __asan_stack_free_0(std::uintptr_t addr, std::size_t size) { PRINT_SELF; }
ASAN_STUB void __asan_stack_free_1(std::uintptr_t addr, std::size_t size) { PRINT_SELF; }
ASAN_STUB void __asan_stack_free_2(std::uintptr_t addr, std::size_t size) { PRINT_SELF; }
ASAN_STUB void __asan_stack_free_3(std::uintptr_t addr, std::uintptr_t size) { PRINT_SELF; }
ASAN_STUB void __asan_stack_free_4(std::uintptr_t addr, std::size_t size) { PRINT_SELF; }
ASAN_STUB void __asan_stack_free_5(std::uintptr_t addr, std::size_t size) { PRINT_SELF; }
ASAN_STUB void __asan_stack_free_6(std::uintptr_t addr, std::size_t size) { PRINT_SELF; }
ASAN_STUB void __asan_stack_free_7(std::uintptr_t addr, std::size_t size) { PRINT_SELF; }

// memset & memcpy
ASAN_STUB void* __asan_memset(void* ptr, int value, std::size_t size)
{
     std::memset(ptr, value, size);
     return ptr;
}
ASAN_STUB void* __asan_memcpy(void* dest, const void* src, std::size_t size)
{
     std::memcpy(dest, src, size);
     return dest;
}

void NO_ASAN kasan::KeInitialise()
{
     debugging::DbgWrite(u8"KASAN: Initialising...\r\n");

     constexpr std::size_t shadowSize = 0xfffffffffff;

     g_shadowMemory =
         g_kernelProcess->AllocateVirtualMemory(reinterpret_cast<void*>(0xffff'a000'0000'0000), shadowSize,
                                                memory::AllocationFlags::Reserve, memory::MemoryProtection::ReadWrite);
}
