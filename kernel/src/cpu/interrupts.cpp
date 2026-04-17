#include "interrupts.h"
#include <utils/identify.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "../dbg/bugcheck.h"
#include "../device/dpc.h"
#include "../process/taskScheduler.h"
#include "BootVideo.h"
#include "mp.h"
#include "utils/kdbg.h"
#include "utils/memory.h"
#include "utils/operations.h"

std::uint64_t cpu::g_systemBootTimeOffsetSeconds{};

namespace
{
     constexpr std::size_t MaxInterruptVectors = 256;

     struct HandlerNode
     {
          InterruptHandler handler;
          HandlerNode* next = nullptr;
          void* argument = nullptr;

          explicit HandlerNode(InterruptHandler h, void* arg) : handler(h), argument(arg) {}
     };

     struct HandlerList
     {
          HandlerNode* head{};
          std::atomic<bool> lock{false};

          void AcquireLock() noexcept
          {
               bool expected = false;
               while (
                   !lock.compare_exchange_weak(expected, true, std::memory_order::acquire, std::memory_order::relaxed))
               {
                    expected = false;
                    operations::Yield();
               }
          }

          void ReleaseLock() noexcept { lock.store(false, std::memory_order::release); }

          void Add(InterruptHandler h, void* argument)
          {
               auto* node = new HandlerNode(h, argument);
               AcquireLock();
               node->next = head;
               head = node;
               ReleaseLock();
          }

          bool Fire(cpu::IInterruptFrame& frame) const
          {
               for (HandlerNode* node = head; node != nullptr; node = node->next)
               {
                    if (node->handler && node->handler(frame, node->argument)) return true;
               }
               return false;
          }

          ~HandlerList()
          {
               HandlerNode* node = head;
               while (node)
               {
                    auto* next = node->next;
                    delete node;
                    node = next;
               }
          }
     };

     std::array<HandlerList, MaxInterruptVectors> g_interruptHandlers{};
} // namespace

#pragma pack(push, 1)
struct ACPIAddress
{
     std::uint8_t addressSpace;
     std::uint8_t bitWidth;
     std::uint8_t bitOffset;
     std::uint8_t accessSize;
     std::uint64_t address;
};
struct HPET
{
     ACPISDTHeader header;
     std::uint8_t hardwareRevId;
     std::uint8_t comparatorCount : 5;
     std::uint8_t counterSize : 1;
     std::uint8_t reserved : 1;
     std::uint8_t legacyReplacement : 1;
     std::uint16_t pciVendorId;
     ACPIAddress address;
     std::uint8_t hpetNumber;
     std::uint16_t minimumTick;
     std::uint8_t pageProtection;
};
#pragma pack(pop)

#ifdef ARCH_X8664
cpu::InterruptVector cpu::TimerIrqVector = 0xd0;

#include <emmintrin.h>

struct MADTLocalApic
{
     MADTEntry header;
     std::uint8_t acpiProcessorId;
     std::uint8_t apicId;
     std::uint32_t flags;
};

struct IOAPICEntry
{
     std::uint8_t type;
     std::uint8_t length;
     std::uint8_t ioApicId;
     std::uint8_t flags;
     std::uint32_t ioApicAddress;
     std::uint32_t globalSystemInterruptBase;
};

struct MADTEntryIOAPIC
{
     MADTEntry header;
     std::uint8_t ioApicId;
     std::uint8_t reserved;
     std::uint32_t ioApicAddress;
     std::uint32_t globalSystemInterruptBase;
};

static volatile std::uint32_t* g_ioApic{};
static std::uint32_t g_ioApicGsiBase{};

struct FADT
{
     ACPISDTHeader header;
     std::uint32_t firmwareCtrl;
     std::uint32_t dsdt;
     std::uint8_t reserved;
     std::uint8_t preferredPmProfile;
     std::uint16_t sciInterrupt;
     std::uint32_t smiCommandPort;
     std::uint8_t acpiEnable;
     std::uint8_t acpiDisable;
     std::uint8_t s4BiosReq;
     std::uint8_t pstateControl;
     std::uint32_t pm1aEventBlock;
     std::uint32_t pm1bEventBlock;
     std::uint32_t pm1aControlBlock;
     std::uint32_t pm1bControlBlock;
     std::uint32_t pm2ControlBlock;
     std::uint32_t pmTimerBlock;
     std::uint32_t gpe0Block;
     std::uint32_t gpe1Block;
     std::uint8_t pm1EventLength;
     std::uint8_t pm1ControlLength;
     std::uint8_t pm2ControlLength;
     std::uint8_t pmTimerLength;
     std::uint8_t gpe0Length;
     std::uint8_t gpe1Length;
     std::uint8_t gpe1Base;
     std::uint8_t cStateControl;
     std::uint16_t worstC2Latency;
     std::uint16_t worstC3Latency;
     std::uint16_t flushSize;
     std::uint16_t flushStride;
     std::uint8_t dutyOffset;
     std::uint8_t dutyWidth;
     std::uint8_t dayAlarm;
     std::uint8_t monthAlarm;
     std::uint8_t century;
     std::uint16_t bootArchitectureFlags;
     std::uint8_t reserved2;
     std::uint32_t flags;

     struct GenericAddressStructure
     {
          std::uint8_t addressSpace;
          std::uint8_t bitWidth;
          std::uint8_t bitOffset;
          std::uint8_t accessSize;
          std::uint64_t address;
     };

     GenericAddressStructure resetReg;
     std::uint8_t resetValue;
     std::uint8_t reserved3[3]; // NOLINT

     std::uint64_t xFirmwareControl;
     std::uint64_t xDsdt;

     GenericAddressStructure xPm1aEventBlock;
     GenericAddressStructure xPm1bEventBlock;
     GenericAddressStructure xPm1aControlBlock;
     GenericAddressStructure xPm1bControlBlock;
     GenericAddressStructure xPm2ControlBlock;
     GenericAddressStructure xPmTimerBlock;
     GenericAddressStructure xGpe0Block;
     GenericAddressStructure xGpe1Block;
};

struct BGRT
{
     ACPISDTHeader header;
     std::uint16_t version;
     std::uint8_t status;
     std::uint8_t imageType;
     std::uint64_t imageAddress;
     std::uint32_t imageOffsetX;
     std::uint32_t imageOffsetY;
};

struct InterruptFrame
{
     std::uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
     std::uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
     std::uint64_t vector, errorCode;
     std::uint64_t rip, cs, rflags, rsp, ss;
};

struct X8664InterruptFrame final : cpu::IInterruptFrame // NOLINT
{
     InterruptFrame* frame;
     explicit X8664InterruptFrame(InterruptFrame* frame) : frame(frame) {}

     [[nodiscard]] std::uint64_t GetVector() const final override { return frame->vector; }
     [[nodiscard]] cpu::InterruptError GetError() const final override
     {
          switch (frame->vector)
          {
          case 0x00: return cpu::InterruptError::DivideByZero;
          case 0x06: return cpu::InterruptError::InvalidInstruction;
          case 0x0D: return cpu::InterruptError::ProtectionFault;
          case 0x0E:
               if (frame->errorCode & (1 << 1)) return cpu::InterruptError::MemoryWriteFault;
               if (frame->errorCode & (1 << 2)) return cpu::InterruptError::MemoryExecuteFault;
               return cpu::InterruptError::MemoryReadFault;
          default: return frame->vector >= 0x20 ? cpu::InterruptError::HardwareInterrupt : cpu::InterruptError::Unknown;
          }
     }

     [[nodiscard]] std::uintptr_t GetInstructionPointer() const final override { return frame->rip; }
     [[nodiscard]] std::uintptr_t GetStackPointer() const final override { return frame->rsp; }
     [[nodiscard]] std::uintptr_t GetFaultingAddress() const final override
     {
#ifdef COMPILER_MSVC
          return __readcr2();
#elifdef COMPILER_CLANG
          std::uintptr_t addr{};
          asm volatile("mov %%cr2, %0" : "=r"(addr));
          return addr;
#endif
     }
     [[nodiscard]] void* GetContext() const final override { return frame; }
     void SetContext(void* context) final override { this->frame = reinterpret_cast<InterruptFrame*>(context); }

     void DumpRegisters() const final override
     {
          debugging::DbgWrite(u8"r15 {}\r\n", reinterpret_cast<void*>(frame->r15));
          debugging::DbgWrite(u8"r14 {}\r\n", reinterpret_cast<void*>(frame->r14));
          debugging::DbgWrite(u8"r13 {}\r\n", reinterpret_cast<void*>(frame->r13));
          debugging::DbgWrite(u8"r12 {}\r\n", reinterpret_cast<void*>(frame->r12));
          debugging::DbgWrite(u8"r11 {}\r\n", reinterpret_cast<void*>(frame->r11));
          debugging::DbgWrite(u8"r10 {}\r\n", reinterpret_cast<void*>(frame->r10));
          debugging::DbgWrite(u8"r9  {}\r\n", reinterpret_cast<void*>(frame->r9));
          debugging::DbgWrite(u8"r8  {}\r\n", reinterpret_cast<void*>(frame->r8));
          debugging::DbgWrite(u8"rbp {}\r\n", reinterpret_cast<void*>(frame->rbp));
          debugging::DbgWrite(u8"rdi {}\r\n", reinterpret_cast<void*>(frame->rdi));
          debugging::DbgWrite(u8"rsi {}\r\n", reinterpret_cast<void*>(frame->rsi));
          debugging::DbgWrite(u8"rdx {}\r\n", reinterpret_cast<void*>(frame->rdx));
          debugging::DbgWrite(u8"rcx {}\r\n", reinterpret_cast<void*>(frame->rcx));
          debugging::DbgWrite(u8"rbx {}\r\n", reinterpret_cast<void*>(frame->rbx));
          debugging::DbgWrite(u8"rax {}\r\n", reinterpret_cast<void*>(frame->rax));
          debugging::DbgWrite(u8"vec {}\r\n", reinterpret_cast<void*>(frame->vector));
          debugging::DbgWrite(u8"erc {}\r\n", reinterpret_cast<void*>(frame->errorCode));
          debugging::DbgWrite(u8"rsp {}\r\n", reinterpret_cast<void*>(frame->rsp));
          debugging::DbgWrite(u8"cs  {}\r\n", reinterpret_cast<void*>(frame->cs));
          debugging::DbgWrite(u8"rfl {}\r\n", reinterpret_cast<void*>(frame->rflags));
          debugging::DbgWrite(u8"ss  {}\r\n", reinterpret_cast<void*>(frame->ss));
     }

     void DumpRegistersImpl() const
     {
          debugging::DbgWrite(u8"r15 {}\r\n", reinterpret_cast<void*>(frame->r15));
          debugging::DbgWrite(u8"r14 {}\r\n", reinterpret_cast<void*>(frame->r14));
          debugging::DbgWrite(u8"r13 {}\r\n", reinterpret_cast<void*>(frame->r13));
          debugging::DbgWrite(u8"r12 {}\r\n", reinterpret_cast<void*>(frame->r12));
          debugging::DbgWrite(u8"r11 {}\r\n", reinterpret_cast<void*>(frame->r11));
          debugging::DbgWrite(u8"r10 {}\r\n", reinterpret_cast<void*>(frame->r10));
          debugging::DbgWrite(u8"r9  {}\r\n", reinterpret_cast<void*>(frame->r9));
          debugging::DbgWrite(u8"r8  {}\r\n", reinterpret_cast<void*>(frame->r8));
          debugging::DbgWrite(u8"rbp {}\r\n", reinterpret_cast<void*>(frame->rbp));
          debugging::DbgWrite(u8"rdi {}\r\n", reinterpret_cast<void*>(frame->rdi));
          debugging::DbgWrite(u8"rsi {}\r\n", reinterpret_cast<void*>(frame->rsi));
          debugging::DbgWrite(u8"rdx {}\r\n", reinterpret_cast<void*>(frame->rdx));
          debugging::DbgWrite(u8"rcx {}\r\n", reinterpret_cast<void*>(frame->rcx));
          debugging::DbgWrite(u8"rbx {}\r\n", reinterpret_cast<void*>(frame->rbx));
          debugging::DbgWrite(u8"rax {}\r\n", reinterpret_cast<void*>(frame->rax));
          debugging::DbgWrite(u8"vec {}\r\n", reinterpret_cast<void*>(frame->vector));
          debugging::DbgWrite(u8"erc {}\r\n", reinterpret_cast<void*>(frame->errorCode));
          debugging::DbgWrite(u8"rsp {}\r\n", reinterpret_cast<void*>(frame->rsp));
          debugging::DbgWrite(u8"cs  {}\r\n", reinterpret_cast<void*>(frame->cs));
          debugging::DbgWrite(u8"rfl {}\r\n", reinterpret_cast<void*>(frame->rflags));
          debugging::DbgWrite(u8"ss  {}\r\n", reinterpret_cast<void*>(frame->ss));
     }
};

constexpr cpu::InterruptVector KiHaltIpiVector = 0xFE;
std::atomic<bool> haltInProgress{false};
static std::atomic<std::uint64_t> g_haltedCpus{0};

template <typename... TArgs> void KiHltPrintEx(const char8_t* fmt, TArgs&&... args)
{
     while (haltInProgress.exchange(true, std::memory_order::acq_rel)) operations::Yield();
     debugging::DbgWrite(fmt, std::forward<TArgs>(args)...);
     haltInProgress.store(false, std::memory_order::release);
}

volatile std::uint32_t* g_lapic{};
std::uintptr_t KiPCFromInterruptFrame(void* frame)
{
     auto* iFrame = static_cast<InterruptFrame*>(frame);
     return iFrame->rip;
}

static void KiHaltIpiHandler()
{
     operations::DisableInterrupts();
     const auto cpuId = g_lapic[0x20 / 4] >> 24;

     const auto mask = g_haltedCpus.load(std::memory_order::acquire);
     KiHltPrintEx(u8"[{}] IPI {:b}\r\n", cpuId, mask | (1u << cpuId));
     g_haltedCpus.fetch_or(1u << cpuId, std::memory_order::acq_rel);

     while (true) operations::Halt();
}

void KiBroadcastHaltIpi()
{
     if (!g_lapic) return;

     g_lapic[0x310 / 4] = 0;
     g_lapic[0x300 / 4] = KiHaltIpiVector | (0b000 << 8) | (1 << 14) | (0b11 << 18);

#ifdef COMPILER_MSVC
     __mfence();
#elifdef COMPILER_CLANG
     asm volatile("mfence" ::: "memory");
#endif
}

extern "C" InterruptFrame* KeHandleInterruptFrame(InterruptFrame* frame)
{
     operations::DisableInterrupts();
     if (frame->vector == KiHaltIpiVector) KiHaltIpiHandler();
     if (g_haltedCpus.load(std::memory_order::acquire) != 0)
     {
          const auto cpuId = g_lapic[0x20 / 4] >> 24;

          const auto mask = g_haltedCpus.load(std::memory_order::acquire);
          KiHltPrintEx(u8"[{}] HLT {:b}\r\n", cpuId, mask | (1u << cpuId));
          g_haltedCpus.fetch_or(1u << cpuId, std::memory_order::acq_rel);

          while (true) operations::Halt();
     }
     if (frame->vector == 2)
          while (true) operations::Halt();
     if ((reinterpret_cast<std::uintptr_t>(frame) & 0xF) != 0)
     {
          while (haltInProgress.exchange(true, std::memory_order::acq_rel)) operations::Yield();
          debugging::DbgWrite(u8"Misaligned old IF {:x} in {:x}\r\n", frame, frame->vector);
          haltInProgress.store(false, std::memory_order::release);
     }

     const auto irql = cpu::KeVectorToIrql(frame->vector);
     const auto oldIrql = KeRaiseIrql(irql);

     Defer defer{[oldIrql]() { KeLowerIrql(oldIrql); }};

     X8664InterruptFrame vFrame{frame};
     HandleInterrupt(vFrame);
     auto* newFrame = vFrame.frame;
     if ((reinterpret_cast<std::uintptr_t>(newFrame) & 0xF) != 0)
     {
          KiHltPrintEx(u8"Misaligned new IF {:x} in {:x}\r\n", newFrame, frame->vector);
          __debugbreak();
     }
     KeAcknowledgeInterrupt();
     operations::EnableInterrupts();
     return newFrame;
}

MADT* g_madt{};
MCFG* g_mcfg{};
FADT* g_fadt{};
HPET* g_hpet{};
BGRT* g_bgrt{};

void KeAcknowledgeInterrupt() { g_lapic[0xB0 / 4] = 0; }

static std::atomic<std::uint64_t> lapicTicks{};

static std::atomic<std::uint64_t> g_HpetFemtosecondsPerTick{};
static std::uintptr_t g_HpetBaseVirtual{};
static std::uintptr_t HpetBasePhysical{};

constexpr std::uint64_t HPET_FEMTO_PER_SEC = 1'000'000'000'000'000ULL;
constexpr std::uint64_t HPET_NANO_PER_SEC = 1'000'000'000ULL;

static inline volatile std::uint64_t* HpetMainCounter()
{
     return reinterpret_cast<volatile std::uint64_t*>(g_HpetBaseVirtual + 0xF0);
}

std::uint64_t KiReadHPETRaw() { return *HpetMainCounter(); }

std::uint64_t KiReadHPET()
{
     const std::uint64_t ticks = *HpetMainCounter();
     return (ticks * g_HpetFemtosecondsPerTick.load(std::memory_order::acquire)) / 1'000'000ULL;
}

std::uint64_t KiGetHPETFrequency()
{
     return 1'000'000'000'000'000ULL / g_HpetFemtosecondsPerTick.load(std::memory_order::acquire);
}

std::uint64_t KiGetHPETRawFrequency()
{
     const auto fspt = g_HpetFemtosecondsPerTick.load(std::memory_order::acquire);
     if (fspt == 0) return 0;
     return 1'000'000'000'000'000ULL / fspt;
}

double KeReadHighResolutionTimerMS()
{
     const std::uint64_t ticks = *HpetMainCounter();
     const long double fs = static_cast<long double>(ticks) *
                            static_cast<long double>(g_HpetFemtosecondsPerTick.load(std::memory_order::acquire));
     return static_cast<double>(fs / 1'000'000'000'000.0L);
}

void KiInitialiseHPET()
{
     HpetBasePhysical = g_hpet->address.address;
     g_HpetBaseVirtual = HpetBasePhysical + 0xffff'8000'0000'0000ULL;

     auto& cap = *reinterpret_cast<volatile std::uint64_t*>(g_HpetBaseVirtual + 0x0);
     auto& cfg = *reinterpret_cast<volatile std::uint64_t*>(g_HpetBaseVirtual + 0x10);
     auto& main = *HpetMainCounter();
     auto& t0 = *reinterpret_cast<volatile std::uint64_t*>(g_HpetBaseVirtual + 0x100);
     auto& c0 = *reinterpret_cast<volatile std::uint64_t*>(g_HpetBaseVirtual + 0x108);

     cfg &= ~(1ULL << 0);
     main = 0;

     const std::uint64_t fspt = (cap >> 32) & 0xFFFFFFFFULL;
     g_HpetFemtosecondsPerTick.store(fspt, std::memory_order::release);

     const std::uint64_t ticks_1ms = 1'000'000ULL * 1'000ULL / fspt;
     const std::uint64_t now = main;
     c0 = now + ticks_1ms;

     t0 |= (1ULL << 2);
     t0 &= ~(1ULL << 3);
     t0 |= (1ULL << 1);
     t0 |= (32ULL << 9);

     cfg |= (1ULL << 0);
}

std::uint64_t KiGetLAPICEstimation()
{
     g_lapic[0x320 / 4] = cpu::TimerIrqVector;

     constexpr std::uint32_t divider = 0b0011;
     g_lapic[0x3E0 / 4] = divider;
     constexpr std::uint32_t fullRange = 0xFFFFFFFF;

     auto measure = [&](std::uint32_t loadCount, std::uint32_t spinIters) -> double
     {
          g_lapic[0x380 / 4] = loadCount;
          const auto startHPET = KiReadHPET();
          const std::uint32_t startCount = g_lapic[0x390 / 4];

          for (std::uint32_t i = 0; i < spinIters; ++i) operations::Yield();

          const auto endHPET = KiReadHPET();
          const std::uint32_t endCount = g_lapic[0x390 / 4];
          const std::uint32_t deltaTicks = startCount - endCount;
          const double seconds = double(endHPET - startHPET) / double(KiGetHPETRawFrequency());
          return double(deltaTicks) / seconds;
     };

     const double freq1 = measure(fullRange, 300000);
     debugging::DbgWrite(u8"Pass1: {} Hz\r\n", static_cast<std::uint64_t>(freq1));

     const std::uint32_t refinedLoad =
         std::clamp<std::uint32_t>(static_cast<std::uint32_t>(freq1 / 200.0), 0x10000, 0xFFFFFFFF);

     const double freq2 = measure(refinedLoad, 100000);
     debugging::DbgWrite(u8"Pass2: {} Hz\r\n", static_cast<std::uint64_t>(freq2));

     const std::uint32_t fineLoad =
         std::clamp<std::uint32_t>(static_cast<std::uint32_t>(freq2 / 80.0), 0x1000, refinedLoad);

     const double freq3 = measure(fineLoad, 30000);
     debugging::DbgWrite(u8"Pass3: {} Hz\r\n", static_cast<std::uint64_t>(freq3));

     const std::uint64_t finalHz = static_cast<std::uint64_t>((freq1 + freq2 + freq3) / 3.0);
     debugging::DbgWrite(u8"Approx LAPIC frequency = {}\r\n", finalHz);

     return finalHz;
}

static std::atomic<std::uint32_t> g_lapicFrequency{};

static void KiInitialiseLAPICTimer(std::uintptr_t acpiPhysical)
{
     RSDPDescriptor* lpRsp = reinterpret_cast<RSDPDescriptor*>(acpiPhysical + 0xffff'8000'0000'0000);
     debugging::DbgWrite(u8"RSDPv{} at {}\r\n", lpRsp->revision, lpRsp);

     if (lpRsp->revision >= 2)
     {
          RSDPDescriptor2* lpRsp20 = reinterpret_cast<RSDPDescriptor2*>(lpRsp);
          XSDT* pXsdt = reinterpret_cast<XSDT*>(lpRsp20->xsdtAddress + 0xffff'8000'0000'0000);

          const std::uint32_t entryCount = (pXsdt->header.length - sizeof(ACPISDTHeader)) / sizeof(std::uintptr_t);

          for (std::size_t i = 0; i < entryCount; i++)
          {
               ACPISDTHeader* pHeader =
                   reinterpret_cast<ACPISDTHeader*>(pXsdt->tablePointers[i] + 0xffff'8000'0000'0000);

               if (memcmp(pHeader->signature, "APIC", 4) == 0)
               {
                    g_madt = reinterpret_cast<MADT*>(pHeader);
                    debugging::DbgWrite(u8"Found MADT at {} (len={})\r\n", g_madt, g_madt->header.length);
               }
               else if (memcmp(pHeader->signature, "FADT", 4) == 0 || memcmp(pHeader->signature, "FACP", 4) == 0)
               {
                    g_fadt = reinterpret_cast<FADT*>(pHeader);
                    debugging::DbgWrite(u8"Found FADT at {} (len={})\r\n", g_fadt, g_fadt->header.length);
               }
               else if (memcmp(pHeader->signature, "MCFG", 4) == 0)
               {
                    g_mcfg = reinterpret_cast<MCFG*>(pHeader);
                    debugging::DbgWrite(u8"Found MCFG at {} (len={})\r\n", g_mcfg, g_mcfg->header.length);
               }
               else if (memcmp(pHeader->signature, "HPET", 4) == 0)
               {
                    g_hpet = reinterpret_cast<HPET*>(pHeader);
                    debugging::DbgWrite(u8"Found HPET at {} (len={})\r\n", g_hpet, g_hpet->header.length);
               }
               else if (memcmp(pHeader->signature, "BGRT", 4) == 0)
               {
                    g_bgrt = reinterpret_cast<BGRT*>(pHeader);
                    debugging::DbgWrite(u8"Found BGRT at {} (len={})\r\n", g_bgrt, g_bgrt->header.length);
               }
               else
                    debugging::DbgWrite(u8"Unknown entry {} ('{}')\r\n", pHeader,
                                        reinterpret_cast<const char8_t (&)[5]>(pHeader->signature)); // NOLINT
          }
     }
     else
     {
          RSDT* lpRsdt = reinterpret_cast<RSDT*>(lpRsp->rsdtAddress + 0xffff'8000'0000'0000);

          const std::uint32_t entryCount = (lpRsdt->header.length - sizeof(ACPISDTHeader)) / sizeof(std::uint32_t);

          for (std::size_t i = 0; i < entryCount; i++)
          {
               ACPISDTHeader* pHeader =
                   reinterpret_cast<ACPISDTHeader*>(lpRsdt->tablePointers[i] + 0xffff'8000'0000'0000);

               if (memcmp(pHeader->signature, "APIC", 4) == 0)
               {
                    g_madt = reinterpret_cast<MADT*>(pHeader);
                    debugging::DbgWrite(u8"Found MADT at {} (len={})\r\n", g_madt, g_madt->header.length);
               }
               else if (memcmp(pHeader->signature, "FADT", 4) == 0 || memcmp(pHeader->signature, "FACP", 4) == 0)
               {
                    g_fadt = reinterpret_cast<FADT*>(pHeader);
                    debugging::DbgWrite(u8"Found FADT at {} (len={})\r\n", g_fadt, g_fadt->header.length);
               }
               else if (memcmp(pHeader->signature, "MCFG", 4) == 0)
               {
                    g_mcfg = reinterpret_cast<MCFG*>(pHeader);
                    debugging::DbgWrite(u8"Found MCFG at {} (len={})\r\n", g_mcfg, g_mcfg->header.length);
               }
               else if (memcmp(pHeader->signature, "HPET", 4) == 0)
               {
                    g_hpet = reinterpret_cast<HPET*>(pHeader);
                    debugging::DbgWrite(u8"Found HPET at {} (len={})\r\n", g_hpet, g_hpet->header.length);
               }
               else if (memcmp(pHeader->signature, "BGRT", 4) == 0)
               {
                    g_bgrt = reinterpret_cast<BGRT*>(pHeader);
                    debugging::DbgWrite(u8"Found BGRT at {} (len={})\r\n", g_bgrt, g_bgrt->header.length);
               }
               else
                    debugging::DbgWrite(u8"Unknown entry {} ('{}')\r\n", pHeader,
                                        reinterpret_cast<const char8_t (&)[5]>(pHeader->signature)); // NOLINT
          }
     }

     if (g_madt == nullptr || g_madt->localApicAddress == 0)
          debugging::DbgWrite(u8"[KiInitialiseLAPICTimer] g_madt == nullptr || g_madt->localApicAddress == 0\r\n");

     if (g_hpet != nullptr)
     {
          HpetBasePhysical = g_hpet->address.address;
          KiInitialiseHPET();
          debugging::DbgWrite(u8"HPET found at {}Hz => base={}\r\n", KiGetHPETRawFrequency(),
                              reinterpret_cast<void*>(g_hpet->address.address));
     }

     const std::uint8_t* ptr = g_madt->entries;
     const std::uint8_t* end = reinterpret_cast<const std::uint8_t*>(g_madt) + g_madt->header.length;

     while (ptr < end)
     {
          const auto* entry = reinterpret_cast<const MADTEntry*>(ptr);
          if (entry->type == 1)
          {
               const auto* io = reinterpret_cast<const MADTEntryIOAPIC*>(entry);
               g_ioApic = reinterpret_cast<volatile std::uint32_t*>(static_cast<std::uintptr_t>(io->ioApicAddress) +
                                                                    0xffff'8000'0000'0000);
               g_ioApicGsiBase = io->globalSystemInterruptBase;
               debugging::DbgWrite(u8"IOAPIC found at {} GSI base {}\r\n", g_ioApic, g_ioApicGsiBase);
          }
          ptr += entry->length;
     }

     volatile std::uint32_t* lapicBase = reinterpret_cast<volatile std::uint32_t*>(
         static_cast<std::uintptr_t>(g_madt->localApicAddress) + 0xffff'8000'0000'0000);
     debugging::DbgWrite(u8"LAPIC at {}\r\n", lapicBase);

#ifdef COMPILER_MSVC
     __writemsr(0x1b, __readmsr(0x1b) | (1uz << 11));
#elifdef COMPILER_CLANG
     std::uint32_t low{};
     std::uint32_t high{};
     asm volatile("rdmsr\n"
                  "bts $11, %%eax\n"
                  "wrmsr"
                  : "=a"(low), "=d"(high)
                  : "c"(0x1B)
                  : "memory");
#endif
     lapicBase[0x320 / 4] = (1 << 16);
     lapicBase[0x380 / 4] = 0;

     g_lapic = lapicBase;

     const auto approximation = KiGetLAPICEstimation();
     g_lapicFrequency.store(static_cast<std::uint32_t>(approximation), std::memory_order::release);

     lapicBase[0xF0 / 4] = 0x1FF;
     lapicBase[0xB0 / 4] = 0;

     // g_interruptHandlers[KiHaltIpiVector].Add(KiHaltIpiHandler, nullptr);
}

static inline void IoApicWrite(std::uint32_t reg, std::uint32_t value)
{
     g_ioApic[0] = reg;
     g_ioApic[4] = value;
}

static inline std::uint32_t IoApicRead(std::uint32_t reg)
{
     g_ioApic[0] = reg;
     return g_ioApic[4];
}

static void IoApicRouteIrq(std::uint32_t irq, std::uint8_t vector)
{
     const std::uint32_t gsi = irq + g_ioApicGsiBase;
     const std::uint32_t reg = 0x10 + (gsi * 2);
     IoApicWrite(reg + 1, 0);
     IoApicWrite(reg, vector);
}

void KeSetTimerFrequency(std::uint32_t frequency, bool isBSP)
{
     const auto freq = g_lapicFrequency.load(std::memory_order::acquire);
     const auto initialCount = freq / frequency;

     g_lapic[0x320 / 4] = (1 << 16);
     g_lapic[0x380 / 4] = 0;

     constexpr std::uint32_t divider = 0b0011;
     g_lapic[0x3E0 / 4] = divider;
     g_lapic[0x320 / 4] = cpu::TimerIrqVector | (1 << 17);
     g_lapic[0x380 / 4] = initialCount;

     if (isBSP)
          lapicTicks.store((KeReadHighResolutionTimer() * frequency) / KeReadHighResolutionTimerFrequency(),
                           std::memory_order::release);
}

std::uint64_t KeReadLowResolutionTimer() { return lapicTicks.load(std::memory_order::acquire); }
std::uint64_t KeReadHighResolutionTimer() { return KiReadHPET(); }
std::uint64_t KeReadLowResolutionTimerFrequency() { return g_lapicFrequency.load(std::memory_order::acquire); }
std::uint64_t KeReadHighResolutionTimerFrequency() { return KiGetHPETFrequency(); }

std::uint64_t KeCurrentSystemTime()
{
     return static_cast<std::uint64_t>(KeReadHighResolutionTimerMS()) + (cpu::g_systemBootTimeOffsetSeconds * 1000uz);
}

static std::atomic<bool> inBugCheck{false};

bool KiInitialiseInterrupts(std::uintptr_t acpiPhysical)
{
     haltInProgress.store(false, std::memory_order::release);
     inBugCheck.store(false, std::memory_order::release);

     new (&g_interruptHandlers) std::array<HandlerList, MaxInterruptVectors>{};

     KiInitialiseLAPICTimer(acpiPhysical);
     const auto lapic = __readmsr(0x1b) & 0xFFFF'FFFF'FFFF'F000;
     memory::paging::MapPage(memory::paging::GetCurrentPageTable(),
                             memory::PageMapping{.virtualAddress = lapic + 0xffff'8000'0000'0000,
                                                 .physicalAddress = lapic,
                                                 .writable = true,
                                                 .executable = false,
                                                 .cachePolicy = memory::CachePolicy::WriteBack},
                             [](std::size_t) -> void*
                             {
                                  std::uintptr_t page =
                                      memory::physicalAllocator.AllocatePage(memory::PFNUse::PageTable);
                                  if (page == ~0uz) return nullptr;
                                  return reinterpret_cast<void*>(page + memory::virtualOffset);
                             });
     return true;
}

void KeRegisterInterruptHandler(cpu::InterruptVector physical, cpu::InterruptVector vector, InterruptHandler handler,
                                void* argument)
{
     if (vector >= MaxInterruptVectors) return;
     g_interruptHandlers[vector].Add(handler, argument);
     IoApicRouteIrq(physical, vector);
}

struct StackFrame
{
     std::uintptr_t previous;
     std::uintptr_t returnAddress;
};

void KiDumpStack(std::uintptr_t rip, std::uintptr_t rbp, std::uintptr_t rsp)
{
     const auto* currentThread = process::KeCurrentThread();
     const auto stackSize = currentThread == nullptr ? process::ThreadStackSize : currentThread->stackSize;

     auto stackBase = currentThread == nullptr ? 0 : reinterpret_cast<std::uintptr_t>(currentThread->stackBase);
     if (stackBase == 0)
     {
          stackBase = (rsp & ~0xFFF) - stackSize + 0x2000; // 2 pages up, just in case
     }
     auto stackLimit = stackBase + stackSize;

     KiHltPrintEx(u8"Stack dump (RIP={:x}, RBP={:x}, RSP={:x}):\r\n", rip, rbp, rsp);
     for (std::size_t i = 0; i < 64; i++)
     {
          if (rbp <= rsp) break;
          if (rbp < stackBase || rbp >= stackLimit)
          {
               KiHltPrintEx(u8"   RBP={:x} out of bounds [{:x}, {:x})\r\n", rbp, stackBase, stackLimit);
               break;
          }
          StackFrame* frame = reinterpret_cast<StackFrame*>(rbp);
          KiHltPrintEx(u8"   #{} {:x}\r\n", i, frame->returnAddress);
          rbp = frame->previous;
     }
     KiHltPrintEx(u8"End of stack dump\r\n");
}

#elifdef ARCH_ARM64

#include <atomic>
#include <cstdint>

static std::atomic<std::uint64_t> g_armTicks{};
static std::atomic<std::uint32_t> g_armFrequency{10};
cpu::InterruptVector cpu::TimerIrqVector = 30;

#ifdef COMPILER_MSVC
#include <arm64intr.h>
#endif

static inline std::uint64_t ReadCNTFRQ()
{
#ifdef COMPILER_MSVC
     return _ReadStatusReg(ARM64_SYSREG(3, 3, 14, 0, 0));
#elif defined(COMPILER_CLANG)
     std::uint64_t freq{};
     asm volatile("mrs %0, cntfrq_el0" : "=r"(freq));
     return freq;
#endif
}

static inline std::uint64_t ArmReadCNTPCT()
{
#ifdef COMPILER_MSVC
     return _ReadStatusReg(ARM64_SYSREG(3, 3, 14, 0, 1));
#elif defined(COMPILER_CLANG)
     std::uint64_t cnt{};
     asm volatile("mrs %0, cntpct_el0" : "=r"(cnt));
     return cnt;
#endif
}

std::uint64_t KeReadLowResolutionTimer() { return g_armTicks.load(std::memory_order::acquire); }
std::uint64_t KeReadHighResolutionTimer() { return ArmReadCNTPCT(); }
std::uint64_t KeReadLowResolutionTimerFrequency() { return g_armFrequency.load(std::memory_order::acquire); }
std::uint64_t KeReadHighResolutionTimerFrequency() { return ReadCNTFRQ(); }

constexpr cpu::InterruptVector KiHaltIpiVector = 0;

static void KiBroadcastHaltSgi()
{
#ifdef COMPILER_MSVC
     _WriteStatusReg(ARM64_SYSREG(3, 0, 12, 11, 5), (1ULL << 40));
     __isb(0xF);
#elifdef COMPILER_CLANG
     asm volatile("msr icc_sgi1r_el1, %x0\nisb" : : "r"(1ULL << 40) : "memory");
#endif
}

void KiBroadcastHaltIpi() { KiBroadcastHaltSgi(); }

struct InterruptFrame
{
     std::uint64_t x0, x1, x2, x3, x4, x5, x6, x7;
     std::uint64_t x8, x9, x10, x11, x12, x13, x14, x15;
     std::uint64_t x16, x17, x18;
     std::uint64_t x19, x20, x21, x22, x23, x24, x25, x26, x27, x28;
     std::uint64_t fp, lr;
     std::uint64_t vector, esr, far, svc, sp;
};
static_assert(sizeof(InterruptFrame) == 36uz * sizeof(std::uint64_t));

struct ARM64InterruptFrame : cpu::IInterruptFrame // NOLINT
{
     InterruptFrame* frame;
     explicit ARM64InterruptFrame(InterruptFrame* frame) : frame(frame) {}

     [[nodiscard]] std::uint64_t GetVector() const override { return frame->vector; }
     [[nodiscard]] cpu::InterruptError GetError() const override
     {
          switch (frame->vector)
          {
          case 0x03: return cpu::InterruptError::MemoryExecuteFault;
          case 0x04:
          {
               std::uint32_t w = frame->esr & 0x3F;
               bool write = (w & 0b10) != 0;
               return write ? cpu::InterruptError::MemoryWriteFault : cpu::InterruptError::MemoryReadFault;
          }
          case 0x15: return cpu::InterruptError::SupervisorCall;
          case 0x20: return cpu::InterruptError::HardwareInterrupt;
          case 0x21: return cpu::InterruptError::FastInterrupt;
          case 0x25:
          {
               std::uint32_t iss = frame->esr & 0xFFFFFF;
               bool write = (iss >> 6) & 1;
               return write ? cpu::InterruptError::MemoryWriteFault : cpu::InterruptError::MemoryReadFault;
          }
          default: return cpu::InterruptError::Unknown;
          }
     }

     [[nodiscard]] std::uintptr_t GetExtra() const override { return frame->svc; }
     [[nodiscard]] std::uint64_t GetInstructionPointer() const override { return frame->lr; }
     [[nodiscard]] std::uint64_t GetStackPointer() const override { return frame->sp; }
     [[nodiscard]] std::uint64_t GetFaultingAddress() const override { return frame->far; }
     [[nodiscard]] void* GetContext() const override { return this->frame; }
     void SetContext(void* context) override { this->frame = reinterpret_cast<InterruptFrame*>(context); }

     void DumpRegisters() const override
     {
          debugging::DbgWrite(u8"x0  {}\r\n", reinterpret_cast<void*>(frame->x0));
          debugging::DbgWrite(u8"x1  {}\r\n", reinterpret_cast<void*>(frame->x1));
          debugging::DbgWrite(u8"x2  {}\r\n", reinterpret_cast<void*>(frame->x2));
          debugging::DbgWrite(u8"x3  {}\r\n", reinterpret_cast<void*>(frame->x3));
          debugging::DbgWrite(u8"x4  {}\r\n", reinterpret_cast<void*>(frame->x4));
          debugging::DbgWrite(u8"x5  {}\r\n", reinterpret_cast<void*>(frame->x5));
          debugging::DbgWrite(u8"x6  {}\r\n", reinterpret_cast<void*>(frame->x6));
          debugging::DbgWrite(u8"x7  {}\r\n", reinterpret_cast<void*>(frame->x7));
          debugging::DbgWrite(u8"x8  {}\r\n", reinterpret_cast<void*>(frame->x8));
          debugging::DbgWrite(u8"x9  {}\r\n", reinterpret_cast<void*>(frame->x9));
          debugging::DbgWrite(u8"x10 {}\r\n", reinterpret_cast<void*>(frame->x10));
          debugging::DbgWrite(u8"x11 {}\r\n", reinterpret_cast<void*>(frame->x11));
          debugging::DbgWrite(u8"x12 {}\r\n", reinterpret_cast<void*>(frame->x12));
          debugging::DbgWrite(u8"x13 {}\r\n", reinterpret_cast<void*>(frame->x13));
          debugging::DbgWrite(u8"x14 {}\r\n", reinterpret_cast<void*>(frame->x14));
          debugging::DbgWrite(u8"x15 {}\r\n", reinterpret_cast<void*>(frame->x15));
          debugging::DbgWrite(u8"x16 {}\r\n", reinterpret_cast<void*>(frame->x16));
          debugging::DbgWrite(u8"x17 {}\r\n", reinterpret_cast<void*>(frame->x17));
          debugging::DbgWrite(u8"x18 {}\r\n", reinterpret_cast<void*>(frame->x18));
          debugging::DbgWrite(u8"x19 {}\r\n", reinterpret_cast<void*>(frame->x19));
          debugging::DbgWrite(u8"x20 {}\r\n", reinterpret_cast<void*>(frame->x20));
          debugging::DbgWrite(u8"x21 {}\r\n", reinterpret_cast<void*>(frame->x21));
          debugging::DbgWrite(u8"x22 {}\r\n", reinterpret_cast<void*>(frame->x22));
          debugging::DbgWrite(u8"x23 {}\r\n", reinterpret_cast<void*>(frame->x23));
          debugging::DbgWrite(u8"x24 {}\r\n", reinterpret_cast<void*>(frame->x24));
          debugging::DbgWrite(u8"x25 {}\r\n", reinterpret_cast<void*>(frame->x25));
          debugging::DbgWrite(u8"x26 {}\r\n", reinterpret_cast<void*>(frame->x26));
          debugging::DbgWrite(u8"x27 {}\r\n", reinterpret_cast<void*>(frame->x27));
          debugging::DbgWrite(u8"x28 {}\r\n", reinterpret_cast<void*>(frame->x28));
          debugging::DbgWrite(u8" fp {}\r\n", reinterpret_cast<void*>(frame->fp));
          debugging::DbgWrite(u8" lr {}\r\n", reinterpret_cast<void*>(frame->lr));
          debugging::DbgWrite(u8"vec {}\r\n", reinterpret_cast<void*>(frame->vector));
          debugging::DbgWrite(u8"esr {}\r\n", reinterpret_cast<void*>(frame->esr));
          debugging::DbgWrite(u8"far {}\r\n", reinterpret_cast<void*>(frame->far));
          debugging::DbgWrite(u8" sp {}\r\n", reinterpret_cast<void*>(frame->sp));
          if (frame->vector == 0x15) debugging::DbgWrite(u8"SVC {}\r\n", frame->svc);
     }
};

static inline void ArmSetTimerTicks(std::uint64_t ticks)
{
#ifdef COMPILER_MSVC
     _WriteStatusReg(ARM64_SYSREG(3, 0, 14, 2, 0), ticks);
     __isb(0xf);
#else
     asm volatile("msr cntp_tval_el0, %0" : : "r"(ticks));
     asm volatile("isb");
#endif
}

static std::atomic<std::uint64_t> g_nextTimerDeadline{0};

static inline void ArmSetTimerDeadline(std::uint64_t cval)
{
#ifdef COMPILER_MSVC
     _WriteStatusReg(ARM64_SYSREG(3, 3, 14, 2, 2), cval);
     __dsb(0xF);
     __isb(0xF);
#else
     asm volatile("msr cntp_cval_el0, %0\n"
                  "dsb sy\n"
                  "isb\n"
                  :
                  : "r"(cval)
                  : "memory");
#endif
}

static inline void ArmReloadTimer(std::uint32_t frequency)
{
     const std::uint64_t interval = ReadCNTFRQ() / frequency;
     const std::uint64_t now = ArmReadCNTPCT();

     std::uint64_t last = g_nextTimerDeadline.load(std::memory_order::acquire);
     while (last <= now) last += interval;
     g_nextTimerDeadline.store(last, std::memory_order::release);
     ArmSetTimerDeadline(last);
}

static std::uintptr_t g_gicdPhysBase = 0;
static std::uintptr_t g_gicrPhysBase = 0;

static inline std::uint32_t GicReadIAR1()
{
     std::uint32_t iar{};
#ifdef COMPILER_MSVC
     iar = static_cast<std::uint32_t>(_ReadStatusReg(ARM64_SYSREG(3, 0, 12, 12, 0)));
#else
     asm volatile("mrs %x0, icc_iar1_el1" : "=r"(iar));
#endif
     return iar;
}

static inline void GicWriteEOIR1(std::uint32_t intid)
{
#ifdef COMPILER_MSVC
     _WriteStatusReg(ARM64_SYSREG(3, 0, 12, 12, 1), intid);
     __isb(0xf);
#else
     asm volatile("msr icc_eoir1_el1, %x0" : : "r"(static_cast<std::uint64_t>(intid)));
     asm volatile("isb");
#endif
}

void KeAcknowledgeInterrupt() {}

static inline void ArmDisablePhysicalTimer()
{
     std::uint64_t ctl{};
#ifdef COMPILER_MSVC
     ctl = _ReadStatusReg(ARM64_SYSREG(3, 3, 14, 2, 1));
     ctl &= ~1;
     _WriteStatusReg(ARM64_SYSREG(3, 3, 14, 2, 1), ctl);
     __isb(0xf);
#else
     asm volatile("mrs %0, cntp_ctl_el0" : "=r"(ctl));
     ctl &= ~1ULL;
     asm volatile("msr cntp_ctl_el0, %0" : : "r"(ctl));
     asm volatile("isb");
#endif
}

static cpu::IRQL g_intidToIrql[1020]{};

static void GicRouteIrq(std::uint32_t irq, std::uint8_t vector, cpu::IRQL irql)
{
     constexpr std::uintptr_t HhdmOffset = 0xffff'8000'0000'0000ULL;

     if (irq < 1020) g_intidToIrql[irq] = irql;

     const std::uintptr_t regEnable = g_gicdPhysBase + HhdmOffset + 0x100 + static_cast<std::uintptr_t>((irq / 32) * 4);
     *reinterpret_cast<volatile std::uint32_t*>(regEnable) |= (1u << (irq % 32));

     const std::uintptr_t regTarget = g_gicdPhysBase + HhdmOffset + 0x800 + irq;
     *reinterpret_cast<volatile std::uint8_t*>(regTarget) = 1 << 0;
}

void KeRegisterInterruptHandler(cpu::InterruptVector physical, cpu::InterruptVector vector, InterruptHandler handler,
                                void* argument)
{
     if (vector >= MaxInterruptVectors) return;
     g_interruptHandlers[vector].Add(handler, argument);
     const cpu::IRQL irql = cpu::KeVectorToIrql(static_cast<std::uint8_t>(vector));
     GicRouteIrq(physical, static_cast<std::uint8_t>(vector), irql);
}

static inline void ArmEnablePhysicalTimer()
{
     std::uint64_t ctl{};
#ifdef COMPILER_MSVC
     ctl = _ReadStatusReg(ARM64_SYSREG(3, 3, 14, 2, 1));
     ctl |= 1;
     ctl &= ~2;
     _WriteStatusReg(ARM64_SYSREG(3, 3, 14, 2, 1), ctl);
     __isb(0xf);
#else
     asm volatile("mrs %0, cntp_ctl_el0" : "=r"(ctl));
     ctl |= 1;
     ctl &= ~2;
     asm volatile("msr cntp_ctl_el0, %0" : : "r"(ctl));
     asm volatile("isb");
#endif
}

extern "C" InterruptFrame* KeHandleInterruptFrame(InterruptFrame* frame)
{
     operations::DisableInterrupts();
     if (frame->vector == 5)
     {
          const std::uint32_t iar = GicReadIAR1();

          if (iar == 1023u)
          {
               operations::EnableInterrupts();
               return frame;
          }

          const cpu::IRQL irql = (iar < 1020) ? g_intidToIrql[iar] : cpu::IRQL::DeviceNormal;
          const auto oldIrql = KeRaiseIrql(irql);
          Defer defer{[oldIrql]() { KeLowerIrql(oldIrql); }};

          if (iar == static_cast<std::uint32_t>(KiHaltIpiVector))
          {
               GicWriteEOIR1(iar);
               operations::DisableInterrupts();
               while (true) operations::Halt();
          }

          if (iar == static_cast<std::uint32_t>(cpu::TimerIrqVector))
          {
               const std::uint64_t interval = ReadCNTFRQ() / g_armFrequency.load(std::memory_order::acquire);
               const std::uint64_t now = ArmReadCNTPCT();
               std::uint64_t next = g_nextTimerDeadline.load(std::memory_order::acquire);
               while (next <= now) next += interval;
               g_nextTimerDeadline.store(next, std::memory_order::release);
               ArmSetTimerDeadline(next);

               GicWriteEOIR1(iar);

               frame->vector = 0x20;
               frame->svc = iar;
               frame->esr = 0;
               frame->far = 0;
               ARM64InterruptFrame vFrame{frame};
               HandleInterrupt(vFrame);
               operations::EnableInterrupts();
               return vFrame.frame;
          }

          GicWriteEOIR1(iar);

          frame->vector = 0x20;
          frame->svc = iar;
          frame->esr = 0;
          frame->far = 0;
          ARM64InterruptFrame vFrame{frame};
          HandleInterrupt(vFrame);
          operations::EnableInterrupts();
          return vFrame.frame;
     }

#ifdef COMPILER_MSVC
     std::uint64_t esr = _ReadStatusReg(ARM64_SYSREG(3, 0, 5, 2, 0));
     std::uint64_t far = _ReadStatusReg(ARM64_SYSREG(3, 0, 6, 0, 0));
#elifdef COMPILER_CLANG
     std::uint64_t esr{};
     std::uint64_t far{};
     asm volatile("mrs %0, esr_el1\n"
                  "mrs %1, far_el1"
                  : "=r"(esr), "=r"(far));
#endif

     frame->esr = esr;
     frame->far = far;
     frame->vector = (esr >> 26) & 0x3F;
     frame->svc = (frame->vector == 0x15) ? (esr & 0xFFFF) : 0xFFFF;

     ARM64InterruptFrame vFrame{frame};
     HandleInterrupt(vFrame);
     operations::EnableInterrupts();
     return vFrame.frame;
}

struct PPTT
{
     ACPISDTHeader header;
     std::uint32_t reserved;
     std::uint8_t entries[];
}; // NOLINT
struct GTDT
{
     ACPISDTHeader header;
     std::uint32_t blockCount;
     std::uint64_t timerBlockAddress;
     std::uint32_t flags;
     std::uint32_t reserved;
     std::uint8_t entries[];
}; // NOLINT
struct SPCR
{
     ACPISDTHeader header;
     std::uint8_t interfaceType;
     std::uint8_t reserved0;
     std::uint16_t reserved1;
     std::uint64_t baseAddress;
     std::uint8_t interruptType;
     std::uint8_t irq;
     std::uint32_t globalSystemInterrupt;
     std::uint8_t baudRate;
     std::uint8_t parity;
     std::uint8_t stopBits;
     std::uint8_t flowControl;
     std::uint8_t terminalType;
     std::uint8_t reserved2[3]; // NOLINT
     std::uint32_t pciDeviceId;
     std::uint32_t pciVendorId;
     std::uint8_t pciBus;
     std::uint8_t pciDevice;
     std::uint8_t pciFunction;
     std::uint8_t pciFlags;
     std::uint8_t pciSegment;
     std::uint8_t reserved3;
};
struct DBG2
{
     ACPISDTHeader header;
     std::uint16_t infoCount;
     std::uint16_t reserved;
     std::uint8_t entries[];
}; // NOLINT
struct IORT
{
     ACPISDTHeader header;
     std::uint32_t nodeCount;
     std::uint32_t nodeOffset;
     std::uint32_t reserved;
};

#pragma pack(push, 1)
struct IORTNodeHeader
{
     std::uint8_t type;
     std::uint16_t length;
     std::uint8_t revision;
     char identifier[4]; // NOLINT
     std::uint32_t numberOfMappings;
     std::uint32_t refToMappings;
};
#pragma pack(pop)

struct GICv3CPUInterfaceNode
{
     IORTNodeHeader header;
     std::uint32_t id;
     std::uint32_t reserved;
     std::uint64_t baseAddress;
     std::uint32_t gicVersion;
     std::uint32_t reserved1;
     std::uint64_t gicRedistributorBase;
};

struct BGRT
{
     ACPISDTHeader header;
     std::uint16_t version;
     std::uint8_t status;
     std::uint8_t imageType;
     std::uint64_t imageAddress;
     std::uint32_t imageOffsetX;
     std::uint32_t imageOffsetY;
};

MADT* g_madt{};
MCFG* g_mcfg{};
PPTT* g_pptt{};
GTDT* g_gtdt{};
SPCR* g_spcr{};
DBG2* g_dbg2{};
IORT* g_iort{};
BGRT* g_bgrt{};

constexpr std::uint8_t MADT_TYPE_GICC = 11;
constexpr std::uint8_t MADT_TYPE_GICD = 12;
constexpr std::uint8_t MADT_TYPE_GICR = 14;

#pragma pack(push, 1)
struct MADTEntryGICC
{
     MADTEntry header;
     std::uint16_t reserved0;
     std::uint32_t cpuInterfaceNumber;
     std::uint32_t acpiProcessorUid;
     std::uint32_t flags;
     std::uint32_t parkingProtocolVersion;
     std::uint32_t performanceInterruptGsiv;
     std::uint64_t parkedAddress;
     std::uint64_t physicalBaseAddress;
     std::uint64_t gicv;
     std::uint64_t gich;
     std::uint32_t vgicMaintenanceInterrupt;
     std::uint64_t gicRedistributorBaseAddress;
     std::uint64_t mpidr;
     std::uint8_t processorPowerEfficiencyClass;
     std::uint8_t reserved1;
     std::uint16_t speOverflowInterrupt;
};

struct MADTEntryGICD
{
     MADTEntry header;
     std::uint16_t reserved0;
     std::uint32_t gicId;
     std::uint64_t physicalBaseAddress;
     std::uint32_t systemVectorBase;
     std::uint8_t gicVersion;
     std::uint8_t reserved1[3]; // NOLINT
};

struct MADTEntryGICR
{
     MADTEntry header;
     std::uint16_t reserved0;
     std::uint64_t discoveryRangeBaseAddress;
     std::uint32_t discoveryRangeLength;
};
#pragma pack(pop)

static inline void ArmDSBISB()
{
#ifdef COMPILER_MSVC
     __dsb(0xF);
#elifdef COMPILER_CLANG
     asm volatile("dsb sy\n isb" ::: "memory");
#endif
}

static void KiParseMADTForGIC(MADT* madt)
{
     g_gicrPhysBase = 0;

     const std::uint8_t* p = madt->entries;
     const std::uint8_t* end = reinterpret_cast<const std::uint8_t*>(madt) + madt->header.length;

     while (p < end)
     {
          const MADTEntry* e = reinterpret_cast<const MADTEntry*>(p);
          if (e->length < 2) break;

          switch (e->type)
          {
          case MADT_TYPE_GICD:
          {
               const auto* gicd = reinterpret_cast<const MADTEntryGICD*>(e);
               g_gicdPhysBase = static_cast<std::uintptr_t>(gicd->physicalBaseAddress);
               debugging::DbgWrite(u8"MADT: GICD phys={}\r\n", reinterpret_cast<void*>(g_gicdPhysBase));
               break;
          }
          case MADT_TYPE_GICR:
          {
               if (g_gicrPhysBase == 0)
               {
                    const auto* gicr = reinterpret_cast<const MADTEntryGICR*>(e);
                    g_gicrPhysBase = static_cast<std::uintptr_t>(gicr->discoveryRangeBaseAddress);
                    debugging::DbgWrite(u8"MADT: GICR phys={}\r\n", reinterpret_cast<void*>(g_gicrPhysBase));
               }
               break;
          }
          case MADT_TYPE_GICC:
          {
               const auto* gicc = reinterpret_cast<const MADTEntryGICC*>(e);
               if (g_gicrPhysBase == 0 && gicc->gicRedistributorBaseAddress != 0)
               {
                    g_gicrPhysBase = static_cast<std::uintptr_t>(gicc->gicRedistributorBaseAddress);
                    debugging::DbgWrite(u8"MADT: GICR physical (from GICC)={}\r\n",
                                        reinterpret_cast<void*>(g_gicrPhysBase));
               }
               break;
          }
          default: break;
          }
          p += e->length;
     }
}

static volatile std::uint32_t* g_gicd = nullptr;
static volatile std::uint32_t* g_gicr = nullptr;

constexpr std::uint32_t GICD_CTLR = 0x000 / 4;
constexpr std::uint32_t GICD_ISENABLER = 0x100 / 4;
constexpr std::uint32_t GICD_ICENABLER = 0x180 / 4;
constexpr std::uint32_t GICD_IPRIORITYR = 0x400 / 4;
constexpr std::uint32_t GICD_ITARGETSR = 0x800 / 4;
constexpr std::uint32_t GICD_ICFGR = 0xC00 / 4;
constexpr std::uint32_t GICD_IROUTER = 0x6000 / 8;

constexpr std::uint32_t GICR_CTLR = 0x000 / 4;
constexpr std::uint32_t GICR_WAKER = 0x014 / 4;

constexpr std::uint32_t GICR_SGI_BASE = 0x10000;
constexpr std::uint32_t GICR_ISENABLER0 = (GICR_SGI_BASE + 0x100) / 4;
constexpr std::uint32_t GICR_ICENABLER0 = (GICR_SGI_BASE + 0x180) / 4;
constexpr std::uint32_t GICR_IPRIORITYR = (GICR_SGI_BASE + 0x400) / 4;
constexpr std::uint32_t GICR_ICFGR1 = (GICR_SGI_BASE + 0xC04) / 4;

static void KiInitialiseGICv3()
{
     constexpr std::uintptr_t HhdmOffset = 0xffff'8000'0000'0000ULL;

     if (g_gicdPhysBase == 0)
     {
          debugging::DbgWrite(u8"[GIC] No GICD found in MADT\r\n");
          return;
     }
     if (g_gicrPhysBase == 0)
     {
          debugging::DbgWrite(u8"[GIC] No GICR found in MADT\r\n");
          return;
     }

     g_gicd = reinterpret_cast<volatile std::uint32_t*>(g_gicdPhysBase + HhdmOffset);
     g_gicr = reinterpret_cast<volatile std::uint32_t*>(g_gicrPhysBase + HhdmOffset);

     volatile std::uint32_t* gicrRd = g_gicr;
     volatile std::uint32_t* gicrSgi = reinterpret_cast<volatile std::uint32_t*>(g_gicrPhysBase + HhdmOffset + 0x10000);

     g_gicd[0x000 / 4] = 0;
     ArmDSBISB();
     for (std::uint32_t i = 1; i < 32; i++) g_gicd[(0x180 / 4) + i] = 0xFFFFFFFF;
     ArmDSBISB();

     g_gicd[0x000 / 4] = (1u << 5) | (1u << 4) | (1u << 2) | (1u << 1);
     ArmDSBISB();

     std::uint32_t waker = gicrRd[0x014 / 4];
     waker &= ~(1u << 1);
     gicrRd[0x014 / 4] = waker;
     ArmDSBISB();

     g_gicd[(0x180 / 4) + 1] = (1u << 1);
     ArmDSBISB();

     while (gicrRd[0x014 / 4] & (1u << 2))
     {
#ifdef COMPILER_MSVC
          __isb(0xf);
#elifdef COMPILER_CLANG
          asm volatile("isb");
#endif
     }
     debugging::DbgWrite(u8"[GIC] Redistributor awake {}\r\n", gicrSgi);

     gicrSgi[0x080 / 4] = 0xFFFFFFFF;
     gicrSgi[0x480 / 4] = 0x00000000;
     ArmDSBISB();

     gicrSgi[0x180 / 4] = ~(1u << 30);
     gicrSgi[0x100 / 4] = (1u << 30);
     ArmDSBISB();

     volatile std::uint32_t* gicr_ipriorityr =
         reinterpret_cast<volatile std::uint32_t*>(g_gicrPhysBase + HhdmOffset + 0x10000 + 0x400);

     const std::uint8_t clockPrio = static_cast<std::uint8_t>(KeIrqlToApicClass(cpu::IRQL::Clock) << 4);
     std::uint32_t prioWord = gicr_ipriorityr[7];
     prioWord &= ~(0xFFu << 16);
     prioWord |= (static_cast<std::uint32_t>(clockPrio) << 16);
     gicr_ipriorityr[7] = prioWord;
     ArmDSBISB();

     if (cpu::TimerIrqVector < 1020) g_intidToIrql[cpu::TimerIrqVector] = cpu::IRQL::Clock;

     std::uint32_t icfgr1 = gicrSgi[0xC04 / 4];
     icfgr1 &= ~(0b11u << ((30 - 16) * 2));
     gicrSgi[0xC04 / 4] = icfgr1;
     ArmDSBISB();

#ifdef COMPILER_MSVC
     std::uint64_t icc_ctlr = _ReadStatusReg(ARM64_SYSREG(3, 0, 12, 12, 4));
     icc_ctlr &= ~1ULL;
     _WriteStatusReg(ARM64_SYSREG(3, 0, 12, 12, 4), icc_ctlr);
     __isb(0xF);
     _WriteStatusReg(ARM64_SYSREG(3, 0, 4, 6, 0), 0xFFULL);
     __isb(0xF);
     _WriteStatusReg(ARM64_SYSREG(3, 0, 12, 12, 7), 1ULL);
     __isb(0xF);
     std::uint64_t daif = _ReadStatusReg(ARM64_SYSREG(3, 3, 4, 2, 1));
     daif &= ~(1 << 9);
     _WriteStatusReg(ARM64_SYSREG(3, 3, 4, 2, 1), daif);
     __isb(0xF);
#elifdef COMPILER_CLANG
     std::uint64_t icc_ctlr{};
     asm volatile("mrs %0, icc_ctlr_el1" : "=r"(icc_ctlr));
     icc_ctlr &= ~1ULL;
     asm volatile("msr icc_ctlr_el1, %0\nisb" : : "r"(icc_ctlr));
     asm volatile("msr icc_pmr_el1, %x0\nisb" : : "r"(0xFFULL));
     asm volatile("msr icc_igrpen1_el1, %x0\nisb" : : "r"(1ULL));
     asm volatile("msr daifclr, #2\nisb");
#endif

     debugging::DbgWrite(u8"[GIC] GICv3 initialised\r\n");
}

void KeSetTimerFrequency(std::uint32_t frequency, bool isBSP)
{
     g_armFrequency.store(frequency, std::memory_order::release);
     g_armTicks.store((KeReadHighResolutionTimer() * frequency) / KeReadHighResolutionTimerFrequency(),
                      std::memory_order::release);

     const std::uint64_t cntFrq = ReadCNTFRQ();
     const std::uint64_t interval = cntFrq / frequency;
     const std::uint64_t now = ArmReadCNTPCT();

     debugging::DbgWrite(u8"[Timer] cntfrq={} freq={} interval={} now={}\r\n", cntFrq, frequency, interval, now);

     if (cntFrq == 0 || interval == 0)
     {
          debugging::DbgWrite(u8"[Timer] BAD FREQUENCY\r\n");
          return;
     }

#ifdef COMPILER_MSVC
     std::uint64_t ctl = _ReadStatusReg(ARM64_SYSREG(3, 3, 14, 2, 1));
     ctl |= 2ULL;
     _WriteStatusReg(ARM64_SYSREG(3, 3, 14, 2, 1), ctl);
     __isb(0xF);
#elifdef COMPILER_CLANG
     std::uint64_t ctl{};
     asm volatile("mrs %0, cntp_ctl_el0" : "=r"(ctl));
     ctl |= 2ULL;
     asm volatile("msr cntp_ctl_el0, %0\nisb" : : "r"(ctl));
#endif

     const std::uint64_t deadline = now + interval;
     g_nextTimerDeadline.store(deadline, std::memory_order::release);
     ArmSetTimerDeadline(deadline);

     ctl |= 1ULL;
     ctl &= ~2ULL;
#ifdef COMPILER_MSVC
     _WriteStatusReg(ARM64_SYSREG(3, 3, 14, 2, 1), ctl);
     __isb(0xF);
#elifdef COMPILER_CLANG
     asm volatile("msr cntp_ctl_el0, %0\nisb" : : "r"(ctl));
#endif
}

bool KiInitialiseInterrupts(std::uintptr_t acpiPhysical)
{
     g_armFrequency.store(100, std::memory_order::release);
     g_nextTimerDeadline.store(0, std::memory_order::release);

     RSDPDescriptor* lpRsp = reinterpret_cast<RSDPDescriptor*>(acpiPhysical + 0xffff'8000'0000'0000);
     MADT* lpMadt = nullptr;
     debugging::DbgWrite(u8"RSDPv{} at {}\r\n", lpRsp->revision, lpRsp);

     if (lpRsp->revision >= 2)
     {
          RSDPDescriptor2* lpRsp20 = reinterpret_cast<RSDPDescriptor2*>(lpRsp);
          XSDT* pXsdt = reinterpret_cast<XSDT*>(lpRsp20->xsdtAddress + 0xffff'8000'0000'0000);
          debugging::DbgWrite(u8"XSDTv{} at {}\r\n", pXsdt->header.revision, pXsdt);

          const std::uint32_t entryCount = (pXsdt->header.length - sizeof(ACPISDTHeader)) / sizeof(std::uintptr_t);
          debugging::DbgWrite(u8"Entries = {}\r\n", entryCount);

          for (std::size_t i = 0; i < entryCount; i++)
          {
               if (pXsdt->tablePointers[i] == 0) break;

               ACPISDTHeader* pHeader =
                   reinterpret_cast<ACPISDTHeader*>(pXsdt->tablePointers[i] + 0xffff'8000'0000'0000);

               if (memcmp(pHeader->signature, "APIC", 4) == 0)
               {
                    lpMadt = reinterpret_cast<MADT*>(pHeader);
                    debugging::DbgWrite(u8"Found MADT at {}\r\n", lpMadt);
               }
               else if (memcmp(pHeader->signature, "MCFG", 4) == 0)
               {
                    g_mcfg = reinterpret_cast<MCFG*>(pHeader);
                    debugging::DbgWrite(u8"Found MCFG at {}\r\n", g_mcfg);
               }
               else if (memcmp(pHeader->signature, "PPTT", 4) == 0)
               {
                    g_pptt = reinterpret_cast<PPTT*>(pHeader);
                    debugging::DbgWrite(u8"Found PPTT at {}\r\n", g_pptt);
               }
               else if (memcmp(pHeader->signature, "GTDT", 4) == 0)
               {
                    g_gtdt = reinterpret_cast<GTDT*>(pHeader);
                    debugging::DbgWrite(u8"Found GTDT at {}\r\n", g_gtdt);
               }
               else if (memcmp(pHeader->signature, "SPCR", 4) == 0)
               {
                    g_spcr = reinterpret_cast<SPCR*>(pHeader);
                    debugging::DbgWrite(u8"Found SPCR at {}\r\n", g_spcr);
               }
               else if (memcmp(pHeader->signature, "DBG2", 4) == 0)
               {
                    g_dbg2 = reinterpret_cast<DBG2*>(pHeader);
                    debugging::DbgWrite(u8"Found DBG2 at {}\r\n", g_dbg2);
               }
               else if (memcmp(pHeader->signature, "BGRT", 4) == 0)
               {
                    g_bgrt = reinterpret_cast<BGRT*>(pHeader);
                    debugging::DbgWrite(u8"Found BGRT at {}\r\n", g_bgrt);
               }
               else if (memcmp(pHeader->signature, "IORT", 4) == 0)
               {
                    g_iort = reinterpret_cast<IORT*>(pHeader);
                    debugging::DbgWrite(u8"Found IORT at {}\r\n", g_iort);
               }
               else
                    debugging::DbgWrite(u8"Unknown entry {} ('{}')\r\n", pHeader,
                                        reinterpret_cast<const char8_t (&)[5]>(pHeader->signature)); // NOLINT
          }
     }
     else
     {
          RSDT* lpRsdt = reinterpret_cast<RSDT*>(lpRsp->rsdtAddress + 0xffff'8000'0000'0000);

          const std::uint32_t entryCount = (lpRsdt->header.length - sizeof(ACPISDTHeader)) / sizeof(std::uint32_t);

          for (std::size_t i = 0; i < entryCount; i++)
          {
               std::uint32_t address = lpRsdt->tablePointers[i];
               ACPISDTHeader* pHeader = reinterpret_cast<ACPISDTHeader*>(address + 0xffff'8000'0000'0000);

               if (address == 0) break;

               if (memcmp(pHeader->signature, "APIC", 4) == 0)
               {
                    lpMadt = reinterpret_cast<MADT*>(pHeader);
                    debugging::DbgWrite(u8"Found MADT at {}\r\n", lpMadt);
               }
               else if (memcmp(pHeader->signature, "MCFG", 4) == 0)
               {
                    g_mcfg = reinterpret_cast<MCFG*>(pHeader);
                    debugging::DbgWrite(u8"Found MCFG at {}\r\n", g_mcfg);
               }
               else if (memcmp(pHeader->signature, "PPTT", 4) == 0)
               {
                    g_pptt = reinterpret_cast<PPTT*>(pHeader);
                    debugging::DbgWrite(u8"Found PPTT at {}\r\n", g_pptt);
               }
               else if (memcmp(pHeader->signature, "GTDT", 4) == 0)
               {
                    g_gtdt = reinterpret_cast<GTDT*>(pHeader);
                    debugging::DbgWrite(u8"Found GTDT at {}\r\n", g_gtdt);
               }
               else if (memcmp(pHeader->signature, "SPCR", 4) == 0)
               {
                    g_spcr = reinterpret_cast<SPCR*>(pHeader);
                    debugging::DbgWrite(u8"Found SPCR at {}\r\n", g_spcr);
               }
               else if (memcmp(pHeader->signature, "DBG2", 4) == 0)
               {
                    g_dbg2 = reinterpret_cast<DBG2*>(pHeader);
                    debugging::DbgWrite(u8"Found DBG2 at {}\r\n", g_dbg2);
               }
               else if (memcmp(pHeader->signature, "BGRT", 4) == 0)
               {
                    g_bgrt = reinterpret_cast<BGRT*>(pHeader);
                    debugging::DbgWrite(u8"Found BGRT at {}\r\n", g_bgrt);
               }
               else
                    debugging::DbgWrite(u8"Unknown entry {} ('{}')\r\n", pHeader,
                                        reinterpret_cast<const char8_t (&)[5]>(pHeader->signature)); // NOLINT
          }
     }

     g_madt = lpMadt;
     if (lpMadt == nullptr)
     {
          debugging::DbgWrite(u8"[KiInitialiseInterrupts] MADT not found!\r\n");
          return false;
     }

     KiParseMADTForGIC(lpMadt);
     KiInitialiseGICv3();
     return true;
}

std::uint64_t KeCurrentSystemTime()
{
     return (KeReadHighResolutionTimer() / (KeReadHighResolutionTimerFrequency() / 1000uz)) +
            (cpu::g_systemBootTimeOffsetSeconds * 1000uz);
}

#endif

void HandleInterrupt(cpu::IInterruptFrame& frameOg)
{
#ifdef ARCH_X8664
     auto* rFrame = reinterpret_cast<X8664InterruptFrame*>(&frameOg);
#elifdef ARCH_ARM64
     auto* rFrame = static_cast<ARM64InterruptFrame*>(&frameOg);
#endif

     if (rFrame->GetError() == cpu::InterruptError::HardwareInterrupt)
     {
          if (rFrame->GetExtra() == cpu::TimerIrqVector)
          {
#ifdef ARCH_X8664
               auto* newContext = process::KiSwitchThread(rFrame->GetContext());
               rFrame->SetContext(newContext);
#elifdef ARCH_ARM64
               auto* newContext = process::KiSwitchThread(rFrame->GetContext());
               rFrame->SetContext(newContext);
#endif

#ifdef ARCH_X8664
               lapicTicks.fetch_add(1, std::memory_order::relaxed);
#elifdef ARCH_ARM64
               g_armTicks.fetch_add(1, std::memory_order::relaxed);
#endif
          }
          else
          {
               g_interruptHandlers[rFrame->GetVector()].Fire(*rFrame);
          }
          return;
     }

     const auto cpuId = g_lapic[0x20 / 4] >> 24;

     operations::DisableInterrupts();

     const auto maskSelf = 1u << cpuId;
     g_haltedCpus.fetch_or(maskSelf, std::memory_order::acq_rel);

     KiHltPrintEx(u8"[{}] Unhandled exception: {:x}\r\n", cpuId, rFrame->frame->vector);
     if (inBugCheck.exchange(true, std::memory_order::acq_rel))
     {
          KiHltPrintEx(u8"[{}] CPU is halting due to another CPU's unhandled exception\r\n", cpuId);
          auto mask = 1u << cpuId;
          g_haltedCpus.fetch_or(mask, std::memory_order::acq_rel);
          while (true) operations::Halt();
     }

     const auto cpus = KeCPUCount();
     KiBroadcastHaltIpi();
     const auto maskAll = (1u << cpus) - 1;
     while (g_haltedCpus.load(std::memory_order::acquire) != maskAll) operations::Yield();
     KiHltPrintEx(u8"All other CPUs halted ({}), halting self\r\n", g_haltedCpus.load(std::memory_order::acquire));

     rFrame->DumpRegistersImpl();
     KiHltPrintEx(u8"Vector  = {}\r\n", reinterpret_cast<void*>(rFrame->GetVector()));
     KiHltPrintEx(u8"Error   = {}\r\n", ToString(rFrame->GetError()));
     KiHltPrintEx(u8"Where   = {}\r\n", reinterpret_cast<void*>(rFrame->GetInstructionPointer()));
     KiHltPrintEx(u8"Stack   = {}\r\n", reinterpret_cast<void*>(rFrame->GetStackPointer()));
     KiHltPrintEx(u8"Address = {}\r\n", reinterpret_cast<void*>(rFrame->GetFaultingAddress()));
     KiDumpStack(rFrame->GetInstructionPointer(), rFrame->frame->rbp, rFrame->frame->rsp);

     dbg::KeBugCheck(*rFrame);
     operations::DisableInterrupts();
     while (true) operations::Halt();
}

cpu::IRQL KiSetXIrqlPhysical(process::CpuLocal& cpu, cpu::IRQL to)
{
     const auto old = cpu.irql.exchange(to, std::memory_order::acq_rel);

#ifdef ARCH_X8664
     auto* apicTpr = reinterpret_cast<volatile std::uint32_t*>(reinterpret_cast<std::uintptr_t>(g_lapic) + 0x80);
     *apicTpr = (KeIrqlToApicClass(to) << 4) & 0xFF;
#ifdef COMPILER_MSVC
     __mfence();
#elifdef COMPILER_CLANG
     asm volatile("mfence" ::: "memory");
#endif
#elifdef ARCH_ARM64
     const std::uint8_t cls = KeIrqlToApicClass(to);
     const std::uint64_t pmr = (cls == 0) ? 0xFFULL : static_cast<std::uint64_t>(cls << 4);
#ifdef COMPILER_MSVC
     _WriteStatusReg(ARM64_SYSREG(3, 0, 4, 6, 0), pmr);
     __isb(0xF);
#elifdef COMPILER_CLANG
     asm volatile("msr icc_pmr_el1, %x0\nisb" : : "r"(pmr) : "memory");
#endif
#endif
     return old;
}

void KiSetIrqlPhysical(process::CpuLocal& cpu, cpu::IRQL to)
{
     cpu.irql.store(to, std::memory_order::release);

#ifdef ARCH_X8664
     auto* apicTpr = reinterpret_cast<volatile std::uint32_t*>(reinterpret_cast<std::uintptr_t>(g_lapic) + 0x80);
     *apicTpr = (KeIrqlToApicClass(to) << 4) & 0xFF;
#ifdef COMPILER_MSVC
     __mfence();
#elifdef COMPILER_CLANG
     asm volatile("mfence" ::: "memory");
#endif
#elifdef ARCH_ARM64
     const std::uint8_t cls = KeIrqlToApicClass(to);
     const std::uint64_t pmr = (cls == 0) ? 0xFFULL : static_cast<std::uint64_t>(cls << 4);
#ifdef COMPILER_MSVC
     _WriteStatusReg(ARM64_SYSREG(3, 0, 4, 6, 0), pmr);
     __isb(0xF);
#elifdef COMPILER_CLANG
     asm volatile("msr icc_pmr_el1, %x0\nisb" : : "r"(pmr) : "memory");
#endif
#endif
}

cpu::IRQL KeRaiseIrql(cpu::IRQL newIrql)
{
     auto* cpu = process::KeCurrentCpu();
     if (cpu == nullptr) return cpu::IRQL::Passive;
     return KiSetXIrqlPhysical(*cpu, newIrql);
}

void KeLowerIrql(cpu::IRQL newIrql)
{
     auto* cpu = process::KeCurrentCpu();
     if (cpu == nullptr) return;

     cpu::IRQL from = cpu->irql.load(std::memory_order::acquire);

     if (from > cpu::IRQL::Dispatch && newIrql <= cpu::IRQL::Dispatch)
     {
          KiSetIrqlPhysical(*cpu, cpu::IRQL::Dispatch);
          device::KeFlushQueuedDpcs();
     }

     KiSetIrqlPhysical(*cpu, newIrql);
}

#pragma pack(push, 1)
struct BmpFileHeader
{
     std::uint16_t type;
     std::uint32_t size;
     std::uint16_t reserved1;
     std::uint16_t reserved2;
     std::uint32_t offBits;
};

struct BmpInfoHeader
{
     std::uint32_t size;
     std::int32_t width;
     std::int32_t height;
     std::uint16_t planes;
     std::uint16_t bitCount;
     std::uint32_t compression;
     std::uint32_t sizeImage;
     std::int32_t xPelsPerMeter;
     std::int32_t yPelsPerMeter;
     std::uint32_t clrUsed;
     std::uint32_t clrImportant;
};
#pragma pack(pop)

void KeDrawBgrt()
{
     if (g_bgrt == nullptr)
     {
          debugging::DbgWrite(u8"BGRT not found or invalid\r\n");
          return;
     }

     const auto* imagePtr = reinterpret_cast<const std::uint8_t*>(g_bgrt->imageAddress + 0xffff'8000'0000'0000ULL);

     const auto orientationInt = (g_bgrt->status >> 1 & 0b11);
     const auto orientation = orientationInt == 0b00   ? BitmapOrientation::o0
                              : orientationInt == 0b01 ? BitmapOrientation::o90
                              : orientationInt == 0b10 ? BitmapOrientation::o180
                                                       : BitmapOrientation::o270;
     VidDrawBitmap(g_bgrt->imageOffsetX, g_bgrt->imageOffsetY, imagePtr);
}
