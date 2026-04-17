#include "pcie.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include "../cpu/interrupts.h"
#include "../kinit.h"
#include "device.h"
#include "utils/kdbg.h"
#include "utils/memory.h"
#include "utils/operations.h"

extern MCFG* g_mcfg;
static std::uint8_t g_nextBus = 1;

struct PciResourceWindow
{
     std::uint64_t memBase{};
     std::uint64_t memLimit{};

     std::uint64_t prefetchBase{};
     std::uint64_t prefetchLimit{};

     std::uint32_t ioBase{};
     std::uint32_t ioLimit{};
};

std::uint64_t KiPCIGetBarSize(volatile PCIHeader* hdr, int index)
{
     auto* barPtr = reinterpret_cast<volatile std::uint32_t*>(reinterpret_cast<std::uintptr_t>(hdr) + 0x10 + index * 4);

     const std::uint32_t original = *barPtr;

     *barPtr = 0xFFFFFFFF;
     const std::uint32_t sizeMask = *barPtr;

     *barPtr = original;

     if (sizeMask == 0 || sizeMask == 0xFFFFFFFF) return 0;

     if ((original & 1) == 0)
     {
          std::uint32_t mask = sizeMask & ~0xF;
          return (~mask + 1);
     }

     return 0;
}

static bool KIIsDevicePresent(std::uintptr_t base, std::uint8_t bus, std::uint8_t device, std::uint8_t function)
{
     const std::uintptr_t address = (base + (static_cast<std::uintptr_t>(bus) << 20)) |
                                    (static_cast<std::uintptr_t>(device) << 15) |
                                    (static_cast<std::uintptr_t>(function) << 12);
     auto* hdr = reinterpret_cast<volatile PCIHeader*>(address);
     return hdr->vendorId != 0xFFFF;
}

static bool KiIsDeviceMultifunction(std::uintptr_t base, std::uint8_t bus, std::uint8_t device)
{
     const std::uintptr_t address =
         base + (static_cast<std::uintptr_t>(bus) << 20) + (static_cast<std::uintptr_t>(device) << 15);
     auto* hdr = reinterpret_cast<volatile PCIHeader*>(address);
     return (hdr->headerType & 0x80) != 0;
}
extern volatile std::uint32_t* g_lapic;

bool KiEnableMSI(volatile PCIHeaderType0* hdr, std::uint8_t vector)
{
     if (!(hdr->status & (1 << 4))) return false;

     std::uint8_t capPtr = hdr->capabilitiesPtr;

     while (capPtr)
     {
          auto* cap = reinterpret_cast<volatile std::uint8_t*>(reinterpret_cast<std::uintptr_t>(hdr) + capPtr);

          const std::uint8_t id = cap[0];
          const std::uint8_t next = cap[1];

          if (id == 0x05)
          {
               auto* msi = reinterpret_cast<volatile std::uint32_t*>(cap);

               const bool is64 = (msi[1] >> 16) & 1;

               msi[2] = reinterpret_cast<std::uintptr_t>(g_lapic) - 0xffff'8000'0000'0000;
               msi[3] = vector;

               if (is64) msi[4] = vector;

               msi[1] |= 1; // enable

               return true;
          }

          capPtr = next;
     }

     return false;
}

PciResourceWindow KiPCIEScanBus(std::uintptr_t base, std::uint16_t segment, std::uint8_t bus, std::uint8_t startDevice,
                                std::uint8_t endDevice, PCIDevice* parent);

void KeGetDeviceBARs(volatile PCIHeader* hdr, PciBar (&bars)[6]) // NOLINT
{
     const auto headerType = hdr->headerType & 0x7f;

     if (headerType == 2) return;
     if (headerType == 1)
     {
          auto* hdr1 = reinterpret_cast<volatile PCIHeaderType1*>(hdr);
          bars[0].value = hdr1->bar0;
          bars[0].isMmio = (hdr1->bar0 & 1) == 0;
          bars[0].is64bit = false;
          bars[0].address = hdr1->bar0 & (bars[0].isMmio ? ~0xF : ~0x3);
          bars[0].isPrefetchable = false;
          bars[1].value = hdr1->bar1;
          bars[1].isMmio = (hdr1->bar1 & 1) == 0;
          bars[1].is64bit = false;
          bars[1].address = hdr1->bar1 & (bars[1].isMmio ? ~0xF : ~0x3);
          bars[1].isPrefetchable = false;
          return;
     }
     if (headerType == 0)
     {
          auto* hdr0 = reinterpret_cast<volatile PCIHeaderType0*>(hdr);
          for (int i = 0; i < 6; i++)
          {
               const std::uint32_t barVal = *(&hdr0->bar0 + i);
               bars[i].value = barVal;
               bars[i].isMmio = (barVal & 1) == 0;

               if (bars[i].isMmio)
               {
                    const std::uint32_t type = (barVal >> 1) & 0x3;
                    bars[i].is64bit = type == 0x2;

                    if (bars[i].is64bit && i < 5)
                    {
                         bars[i].address = static_cast<std::uint64_t>(barVal & ~0xF) |
                                           (static_cast<std::uint64_t>(*(&hdr0->bar0 + i + 1)) << 32);
                         i++;
                    }
                    else
                    {
                         bars[i].address = barVal & ~0xF;
                    }
               }
               else // I/O BAR
               {
                    bars[i].is64bit = false;
                    bars[i].address = barVal & ~0x3;
               }
          }
          return;
     }

     debugging::DbgWrite(u8"Unknown PCI header type {}, cannot read BARs\r\n", headerType);
}

std::uintptr_t KeFindControllerBAR(volatile PCIHeader* hdr, std::uint8_t progIf, PciBar (&bars)[6]) // NOLINT
{
     for (auto& bar : bars)
     {
          if (bar.address == 0) continue;

          if (progIf == 0x00 && !bar.isMmio) return static_cast<std::uintptr_t>(bar.address); // UHCI
          if (progIf == 0x10 && bar.isMmio) return static_cast<std::uintptr_t>(bar.address);  // OHCI
          if (progIf == 0x20 && bar.isMmio) return static_cast<std::uintptr_t>(bar.address);  // EHCI
          if (progIf == 0x30 && bar.isMmio) return static_cast<std::uintptr_t>(bar.address);  // XHCI
     }
     return 0;
}

static std::uintptr_t g_pcieBase = 0;

static PciResourceWindow KiPCIEScanFunction(std::uintptr_t base, std::uint16_t segment, std::uint8_t bus,
                                            std::uint8_t device, std::uint8_t function, PCIDevice* parent)
{
     PciResourceWindow window{};

     std::size_t indentSize{};
     device::Device* temp = parent;
     while (temp)
     {
          indentSize++;
          temp = temp->parent;
     }

     auto* hdr = reinterpret_cast<volatile PCIHeader*>((base + (static_cast<std::uintptr_t>(bus) << 20)) |
                                                       (static_cast<std::uintptr_t>(device) << 15) |
                                                       (static_cast<std::uintptr_t>(function) << 12));

     if (hdr->vendorId == 0xFFFF) return {};

     for (std::size_t i = 0; i < indentSize; i++) debugging::DbgWrite(u8"  ");
     if ((hdr->headerType & 0x80) != 0)
          debugging::DbgWrite(u8"    Found Multi-PCI{} device {:x}:{:x} class {:x}:{:x} at {}.{}.{}.{}\r\n",
                              hdr->headerType & 0x7F, hdr->vendorId, hdr->deviceId, hdr->baseClass, hdr->subclass, bus,
                              device, function);
     else
          debugging::DbgWrite(u8"    Found PCI{} device {:x}:{:x} class {:x}:{:x} at {}.{}.{}.{}\r\n",
                              hdr->headerType & 0x7F, hdr->vendorId, hdr->deviceId, hdr->baseClass, hdr->subclass, bus,
                              device, function);

     static std::size_t deviceIndex = 0;

     std::array<char, 64> name{};
     char* next = name.data();

     std::memcpy(next, u8"PCI", 3);
     next += 3;
     *(next++) = '\\';

     auto writeHex = [&](auto value, int width)
     {
          std::array<char, 16> buffer{};
          char* p = buffer.data() + width;
          for (int i = 0; i < width; ++i)
          {
               std::uint8_t digit = value & 0xF;
               *(--p) = static_cast<char>((digit < 10) ? '0' + digit : 'A' + (digit - 10));
               value >>= 4;
          }
          std::memcpy(next, buffer.data(), width);
          next += width;
     };

     auto writeDecimal = [&](auto value)
     {
          char* start = next;
          next += std::to_chars(next, name.data() + 64, value, 10).ptr - next;
          return start;
     };

     writeHex(hdr->vendorId, 4);
     *(next++) = ':';

     writeHex(hdr->deviceId, 4);
     *(next++) = '_';

     writeHex(hdr->baseClass, 4);
     *(next++) = ':';

     writeHex(hdr->subclass, 4);
     *(next++) = '_';

     writeHex(hdr->revisionId, 2);
     *(next++) = '_';

     writeDecimal(bus);
     *(next++) = ':';
     writeDecimal(device);
     *(next++) = ':';
     writeDecimal(function);
     *(next++) = '\\';

     writeDecimal(deviceIndex++);

     *next = '\0';

     object::ObjectAttributes attributes{.name = name.data(),
                                         .type = object::ObjectType::Device,
                                         .bodySize = sizeof(PCIDevice),
                                         .destructor = +[](void*) noexcept {},
                                         .desiredAccess = object::AccessRights::All};
     auto* hObject = object::ObCreateObjectIn(*g_kernelProcess, attributes, "Device");
     if (!hObject)
     {
          debugging::DbgWrite(u8"Failed to create object for PCI device {:x}:{:x}\r\n", hdr->vendorId, hdr->deviceId);
          return {};
     }

     auto* lpDevice = new (object::ObGetBody<void>(*g_kernelProcess, hObject, object::ObjectType::Device)) PCIDevice{};
     if (!lpDevice)
     {
          debugging::DbgWrite(u8"Failed to get body for PCI device {:x}:{:x}\r\n", hdr->vendorId, hdr->deviceId);
          object::ObCloseHandle(*g_kernelProcess, hObject);
          return {};
     }
     lpDevice->vendorId = hdr->vendorId;
     lpDevice->deviceId = hdr->deviceId;
     lpDevice->baseClass = hdr->baseClass;
     lpDevice->subClass = hdr->subclass;
     lpDevice->progIf = hdr->progIf;
     lpDevice->header = hdr;
     lpDevice->segment = segment;
     lpDevice->bus = static_cast<std::uint8_t>((reinterpret_cast<std::uintptr_t>(hdr) >> 20) & 0xFF);
     lpDevice->device = static_cast<std::uint8_t>((reinterpret_cast<std::uintptr_t>(hdr) >> 15) & 0x1F);
     lpDevice->function = static_cast<std::uint8_t>((reinterpret_cast<std::uintptr_t>(hdr) >> 12) & 0x07);
     lpDevice->parent = parent;

     constexpr std::uint8_t capabilityMsi = 0x05;
     constexpr std::uint8_t capabilityPcie = 0x9;
     constexpr std::uint8_t capabilityPower = 0x10;
     constexpr std::uint8_t capabilityMsiX = 0x11;

     KeGetDeviceBARs(hdr, lpDevice->bars);

     if ((hdr->headerType & 0x7F) == 1)
     {
          if (hdr->baseClass != 0x06 || hdr->subclass != 0x04)
          {
               debugging::DbgWrite(u8"Non-bridge PCIe device, skipping bus scan\r\n");
               return {};
          }
          auto* bridge = reinterpret_cast<volatile PCIHeaderType1*>(hdr);

          const std::uint8_t secondary = g_nextBus++;
          const std::uint8_t subordinate = 0xFF;

          bridge->secondaryBus = 0;
          bridge->subordinateBus = 0;

          bridge->primaryBus = bus;
          bridge->secondaryBus = secondary;
          bridge->subordinateBus = subordinate;

          KiPCIEScanBus(base, segment, secondary, 0, 31, lpDevice);

          bridge->subordinateBus = g_nextBus - 1;

          debugging::DbgWrite(u8"      [ASSIGNED] primary {} -> secondary {}, subordinate {}\r\n", bridge->primaryBus,
                              bridge->secondaryBus, bridge->subordinateBus);

          // auto* bridgeHdr = reinterpret_cast<volatile PCIHeaderType1*>(hdr);

          // for (std::size_t i = 0; i < indentSize; i++) debugging::DbgWrite(u8"  ");
          // debugging::DbgWrite(u8"      PCIe bridge: primary {} -> secondary {}, subordinate {} \r\n",
          //                     bridgeHdr->primaryBus, bridgeHdr->secondaryBus, bridgeHdr->subordinateBus);

          // std::uint16_t memBase = bridgeHdr->memoryBase & 0xFFF0;
          // std::uint16_t memLimit = bridgeHdr->memoryLimit & 0xFFF0;
          // std::uint16_t ioBase = bridgeHdr->ioBase & 0xF0;
          // std::uint16_t ioLimit = bridgeHdr->ioLimit & 0xF0;

          // // debugging::DbgWrite(u8"        IO {:x}-{:x} MEM {:x}-{:x}\r\n",
          // // static_cast<std::uint32_t>(ioBase) << 8,
          // //                     static_cast<std::uint32_t>(ioLimit) << 8,
          // //                     static_cast<std::uint32_t>(memBase) << 16,
          // //                     static_cast<std::uint32_t>(memLimit) << 16);

          // KiPCIEScanBus(base, segment, bridgeHdr->secondaryBus, 0, 31, lpDevice);
     }
     else if ((hdr->headerType & 0x7F) == 0)
     {
          auto* hdr0 = reinterpret_cast<volatile PCIHeaderType0*>(hdr);
          hdr->command |= (1 << 2);
          hdr->command |= (1 << 10);
          if (hdr->status & (1 << 4))
          {
               debugging::DbgWrite(u8"      Device supports MSI, attempting to enable...");

               if (KiEnableMSI(hdr0, 0x20 + (deviceIndex - 1))) debugging::DbgWrite(u8" success\r\n");
               else
                    debugging::DbgWrite(u8" failed\r\n");
          }
     }

     return window;
}

PciResourceWindow KiPCIEScanBus(std::uintptr_t base, std::uint16_t segment, std::uint8_t bus, std::uint8_t startDevice,
                                std::uint8_t endDevice, PCIDevice* parent)
{
     PciResourceWindow busWindow{};

     device::Device* temp = parent;
     while (temp)
     {
          debugging::DbgWrite(u8"  ");
          temp = temp->parent;
     }

     debugging::DbgWrite(u8"  Scanning PCIe bus {} (segment {})\r\n", bus, segment);
     for (std::uint8_t device = startDevice; device <= endDevice; device++)
     {
          if (!KIIsDevicePresent(base, bus, device, 0)) continue;
          auto devWindow = KiPCIEScanFunction(base, segment, bus, device, 0, parent);

          if (devWindow.memBase != 0)
          {
               if (busWindow.memBase == 0 || devWindow.memBase < busWindow.memBase)
                    busWindow.memBase = devWindow.memBase;

               busWindow.memLimit = std::max(devWindow.memLimit, busWindow.memLimit);
          }

          if (KiIsDeviceMultifunction(base, bus, device))
               for (std::uint8_t function = 1; function < 8; function++)
                    if (KIIsDevicePresent(base, bus, device, function))
                    {
                         auto funcWindow = KiPCIEScanFunction(base, segment, bus, device, function, parent);
                         if (funcWindow.memBase != 0)
                         {
                              if (busWindow.memBase == 0 || funcWindow.memBase < busWindow.memBase)
                                   busWindow.memBase = funcWindow.memBase;

                              busWindow.memLimit = std::max(funcWindow.memLimit, busWindow.memLimit);
                         }
                    }
     }
     return busWindow;
}

void KeInitialisePCIE()
{
     if (g_mcfg == nullptr)
     {
          debugging::DbgWrite(u8"MCFG table not found, PCI-e initialisation failed\r\n");
          return;
     }

     const std::size_t entries = (g_mcfg->header.length - sizeof(ACPISDTHeader)) / sizeof(MCFGAllocationClass);

     debugging::DbgWrite(u8"Scanning the PCI-e bus\r\n");
     const auto start = KeReadHighResolutionTimer();
     for (std::size_t i = 0; i < entries; i++)
     {
          const auto& alloc = g_mcfg->allocationClasses[i];
          // debugging::DbgWrite(u8"  MCFG Entry {}: base {}, segment {}, bus {}-{}\r\n", i,
          //                     reinterpret_cast<void*>(alloc.baseAddress), alloc.pciSegmentGroup,
          //                     alloc.startBusNumber, alloc.endBusNumber);

          const std::uintptr_t baseVA = alloc.baseAddress + 0xffff'8000'0000'0000;
          const std::size_t numBuses = alloc.endBusNumber - alloc.startBusNumber + 1;
          const std::size_t ecamSize = numBuses * 32 * 8 * 0x1000;
          const std::size_t pages = (ecamSize + 0xFFF) / 0x1000;

          std::size_t mappedCount{};
          for (std::size_t p = 0; p < pages; ++p)
          {
               const std::uintptr_t pageVA = baseVA + (p * 0x1000);
               const std::uintptr_t pagePhys = alloc.baseAddress + (p * 0x1000);

               if (g_kernelProcess->QueryAddress(pageVA) != nullptr) continue;

               memory::PageMapping mapping{};
               mapping.virtualAddress = pageVA;
               mapping.physicalAddress = pagePhys;
               mapping.size = 0x1000;
               mapping.writable = true;
               mapping.userAccessible = false;
               mapping.cachePolicy = memory::CachePolicy::Uncacheable;
               mapping.executable = false;

               auto ptAllocator = [](std::size_t) -> void*
               {
                    std::uintptr_t page = memory::physicalAllocator.AllocatePage(memory::PFNUse::PageTable);
                    if (page == ~0uz) return nullptr;
                    return reinterpret_cast<void*>(page + memory::virtualOffset);
               };

               if (!memory::paging::MapPage(memory::paging::GetCurrentPageTable(), mapping, ptAllocator))
               {
                    debugging::DbgWrite(u8"  Failed to map ECAM page VA {} -> phys {}\r\n",
                                        reinterpret_cast<void*>(pageVA), reinterpret_cast<void*>(pagePhys));
                    continue;
               }
               mappedCount++;
          }

          g_kernelProcess->ReserveMemoryFixed(baseVA, 0x1000 * pages, memory::MemoryProtection::ReadWrite,
                                              memory::PFNUse::DriverLocked);

          g_pcieBase = baseVA;

          g_nextBus = 1;
          KiPCIEScanBus(baseVA, alloc.pciSegmentGroup, 0, 0, 31, nullptr);
     }
     const auto end = KeReadHighResolutionTimer();
     const auto duration = end - start;
     debugging::DbgWrite(u8"Finished scanning in {} ms\r\n", duration * 1000uz / KeReadHighResolutionTimerFrequency());
}

std::uintptr_t PciConfigAddress(std::uint8_t bus, std::uint8_t device, std::uint8_t func, std::uint8_t reg)
{
     return g_pcieBase | (static_cast<std::uintptr_t>(bus) << 16) | (static_cast<std::uintptr_t>(device) << 11) |
            (static_cast<std::uintptr_t>(func) << 8) | (reg & 0xFC);
}

std::uint16_t KeReadPCIWord(std::uint32_t addr, std::uint8_t reg)
{
     operations::WritePort32(0xCF8, PciConfigAddress((addr >> 8) & 0xFF, (addr >> 3) & 0x1F, addr & 0x7, reg));
     std::uint32_t data = operations::ReadPort32(0xCFC);
     const std::uint8_t shift = (reg & 2) * 8;
     return static_cast<std::uint16_t>((data >> shift) & 0xFFFF);
}

void KeWritePCIWord(std::uint32_t addr, std::uint8_t reg, std::uint16_t value)
{
     operations::WritePort32(0xCF8, PciConfigAddress((addr >> 8) & 0xFF, (addr >> 3) & 0x1F, addr & 0x7, reg));
     std::uint32_t data = operations::ReadPort32(0xCFC);
     const std::uint8_t shift = (reg & 2) * 8;
     data &= ~(0xFFFFu << shift);
     data |= static_cast<std::uint32_t>(value) << shift;
     operations::WritePort32(0xCFC, data);
}
void KeFastWritePCI8(std::uint16_t segment, std::uint16_t bus, std::uint8_t device, std::uint8_t func, std::uint8_t reg,
                     std::uint8_t value)
{
     *reinterpret_cast<volatile std::uint8_t*>(PciConfigAddress(bus, device, func, reg) + 0xffff'8000'0000'0000) =
         value;
}
std::uint8_t KeFastReadPCI8(std::uint16_t segment, std::uint16_t bus, std::uint8_t device, std::uint8_t func,
                            std::uint8_t reg)
{
     return *reinterpret_cast<volatile std::uint8_t*>(PciConfigAddress(bus, device, func, reg) + 0xffff'8000'0000'0000);
}

void KeFastWritePCI16(std::uint16_t segment, std::uint16_t bus, std::uint8_t device, std::uint8_t func,
                      std::uint8_t reg, std::uint16_t value)
{
     *reinterpret_cast<volatile std::uint16_t*>(PciConfigAddress(bus, device, func, reg) + 0xffff'8000'0000'0000) =
         value;
}
std::uint16_t KeFastReadPCI16(std::uint16_t segment, std::uint16_t bus, std::uint8_t device, std::uint8_t func,
                              std::uint8_t reg)
{
     return *reinterpret_cast<volatile std::uint16_t*>(PciConfigAddress(bus, device, func, reg) +
                                                       0xffff'8000'0000'0000);
}

void KeFastWritePCI32(std::uint16_t segment, std::uint16_t bus, std::uint8_t device, std::uint8_t func,
                      std::uint8_t reg, std::uint32_t value)
{
     *reinterpret_cast<volatile std::uint32_t*>(PciConfigAddress(bus, device, func, reg) + 0xffff'8000'0000'0000) =
         value;
}
std::uint32_t KeFastReadPCI32(std::uint16_t segment, std::uint16_t bus, std::uint8_t device, std::uint8_t func,
                              std::uint8_t reg)
{
     return *reinterpret_cast<volatile std::uint32_t*>(PciConfigAddress(bus, device, func, reg) +
                                                       0xffff'8000'0000'0000);
}

void KeFastWritePCI64(std::uint16_t segment, std::uint16_t bus, std::uint8_t device, std::uint8_t func,
                      std::uint8_t reg, std::uint64_t value)
{
     *reinterpret_cast<volatile std::uint64_t*>(PciConfigAddress(bus, device, func, reg) + 0xffff'8000'0000'0000) =
         value;
}
std::uint64_t KeFastReadPCI64(std::uint16_t segment, std::uint16_t bus, std::uint8_t device, std::uint8_t func,
                              std::uint8_t reg)
{
     return *reinterpret_cast<volatile std::uint64_t*>(PciConfigAddress(bus, device, func, reg) +
                                                       0xffff'8000'0000'0000);
}
