#pragma once

#include <cstddef>
#include "device.h"
#include "utils/struct.h"
#pragma once
#include <cstdint>

#pragma pack(push, 1)

struct PCIHeader
{
     std::uint16_t vendorId;
     std::uint16_t deviceId;
     std::uint16_t command;
     std::uint16_t status;
     std::uint8_t revisionId;
     std::uint8_t progIf;
     std::uint8_t subclass;
     std::uint8_t baseClass;
     std::uint8_t cacheLineSize;
     std::uint8_t latencyTimer;
     std::uint8_t headerType;
     std::uint8_t bist;
};

struct PCIHeaderType0 : PCIHeader
{
     std::uint32_t bar0;
     std::uint32_t bar1;
     std::uint32_t bar2;
     std::uint32_t bar3;
     std::uint32_t bar4;
     std::uint32_t bar5;
     std::uint32_t cardbusCisPtr;
     std::uint16_t subsystemVendorId;
     std::uint16_t subsystemId;
     std::uint32_t expansionRomBar;
     std::uint8_t capabilitiesPtr;
     std::uint8_t reserved1[3]; // NOLINT
     std::uint32_t reserved2;
     std::uint8_t interruptLine;
     std::uint8_t interruptPin;
     std::uint8_t minGrant;
     std::uint8_t maxLatency;
};
struct PCIHeaderType1 : PCIHeader
{
     std::uint32_t bar0;
     std::uint32_t bar1;
     std::uint8_t primaryBus;
     std::uint8_t secondaryBus;
     std::uint8_t subordinateBus;
     std::uint8_t secondaryLatencyTimer;
     std::uint8_t ioBase;
     std::uint8_t ioLimit;
     std::uint16_t secondaryStatus;
     std::uint16_t memoryBase;
     std::uint16_t memoryLimit;
     std::uint16_t prefetchableMemoryBase;
     std::uint16_t prefetchableMemoryLimit;
     std::uint32_t prefetchableBaseUpper32;
     std::uint32_t prefetchableLimitUpper32;
     std::uint16_t ioBaseUpper16;
     std::uint16_t ioLimitUpper16;
     std::uint8_t capabilitiesPtr;
     std::uint8_t reserved[3]; // NOLINT
     std::uint32_t expansionRomBar;
     std::uint8_t interruptLine;
     std::uint8_t interruptPin;
     std::uint16_t bridgeControl;
};
struct PCIHeaderType2 : PCIHeader
{
     std::uint32_t cardbusSocketReg;
     std::uint8_t offsetOfCapabilitiesList;
     std::uint8_t reserved;
     std::uint16_t secondaryStatus;
     std::uint8_t pciBusNumber;
     std::uint8_t cardbusBusNumber;
     std::uint8_t subordinateBusNumber;
     std::uint8_t cardbusLatencyTimer;
     std::uint32_t memoryBase0;
     std::uint32_t memoryLimit0;
     std::uint32_t memoryBase1;
     std::uint32_t memoryLimit1;
     std::uint32_t ioBase0;
     std::uint32_t ioLimit0;
     std::uint32_t ioBase1;
     std::uint32_t ioLimit1;
     std::uint8_t interruptLine;
     std::uint8_t interruptPin;
     std::uint16_t bridgeControl;
     std::uint16_t subsystemVendorId;
     std::uint16_t subsystemId;
     std::uint32_t pcCardLegacyModeBaseAddr;
};

struct XhciCap
{
     std::uint8_t capLength;
     std::uint8_t reserved;
     std::uint16_t hcVersion;
     std::uint32_t hcsParams1;
     std::uint32_t hcsParams2;
};

struct EhciCap
{
     std::uint8_t capLength;
     std::uint8_t reserved;
     std::uint16_t hcVersion;
     std::uint32_t hcsParams;
};

namespace pciCapabilities
{
     struct MSI
     {
          std::uint16_t messageControl;
          std::uint8_t nextPointer;
          std::uint8_t capabilityID;

          std::uint32_t messageAddressLow;

          std::uint32_t messageAddressHigh;

          std::uint16_t messageData;
          std::uint16_t reserved;

          std::uint32_t maskBits;
          std::uint32_t pendingBits;
     };

     struct MSIEntry
     {
          std::uint8_t enabled : 1;                // 1
          std::uint8_t multipleMessageCapable : 3; // 1-3
          std::uint8_t multipleMessageEnable : 3;  // 4-6
          std::uint8_t is64Bit : 1;                // 7
          std::uint8_t perVectorMasking : 1;       // 8
          std::uint8_t reserved : 7;               // 9-15
     };

     struct MSIX
     {
          std::uint16_t messageControl;
          std::uint8_t nextPointer;
          std::uint8_t capabilityID;

          std::uint32_t tableOffsetAndBIR;
          std::uint32_t pbaOffsetAndBIR;
     };

     struct MSIXEntry
     {
          std::uint32_t messageAddressLow;
          std::uint32_t messageAddressHigh;
          std::uint32_t messageData;
          std::uint32_t vectorControl;
     };

     struct PCIe
     {
          std::uint16_t deviceCapabilities;
          std::uint8_t nextPointer;
          std::uint8_t capabilityID;

          std::uint16_t deviceControl;
          std::uint16_t deviceStatus;

          std::uint32_t linkCapabilities;
          std::uint16_t linkControl;
          std::uint16_t linkStatus;

          std::uint32_t slotCapabilities;
          std::uint16_t slotControl;
          std::uint16_t slotStatus;
     };
     struct PowerManagement
     {
          std::uint16_t pmCapabilities;
          std::uint8_t nextPointer;
          std::uint8_t capabilityID;

          std::uint16_t pmControlStatus;
          std::uint16_t reserved;
     };
} // namespace pciCapabilities

#pragma pack(pop)

struct PciBar
{
     std::uint32_t value{};
     bool isMmio{};
     bool is64bit{};
     std::uint64_t address{};
     bool isPrefetchable{};
};

struct PCICapability
{
     std::uint8_t id;
     std::uint8_t next;
     std::uintptr_t address;
     std::size_t size;
};

struct PCIDevice : device::Device
{
     explicit PCIDevice() : Device{.type = device::DeviceType::PCI} {}

     std::uint16_t vendorId{};
     std::uint16_t deviceId{};
     std::uint8_t baseClass{};
     std::uint8_t subClass{};
     std::uint8_t progIf{};
     PciBar bars[6]; // NOLINT

     volatile PCIHeader* header{};
     std::uint16_t segment{};
     std::uint8_t bus{};
     std::uint8_t device{};
     std::uint8_t function{};

     structures::LinkedList<PCICapability> capabilities{};

     void* driverData{};

     [[nodiscard]] std::uint32_t ReadConfig32(std::size_t offset) const;
     void WriteConfig32(std::size_t offset, std::uint32_t value) const;
};

void KeInitialisePCIE();
void KeGetDeviceBARs(volatile PCIHeader* hdr, PciBar (&bars)[6]);                                    // NOLINT
std::uintptr_t KeFindControllerBAR(volatile PCIHeader* hdr, std::uint8_t progIf, PciBar (&bars)[6]); // NOLINT
void KeWritePCIWord(std::uint32_t addr, std::uint8_t reg, std::uint16_t value);
std::uint16_t KeReadPCIWord(std::uint32_t addr, std::uint8_t reg);
std::uintptr_t PciConfigAddress(std::uint8_t bus, std::uint8_t device, std::uint8_t func, std::uint8_t reg);

void KeFastWritePCI8(std::uint16_t segment, std::uint16_t bus, std::uint8_t device, std::uint8_t func, std::uint8_t reg,
                     std::uint8_t value);
std::uint8_t KeFastReadPCI8(std::uint16_t segment, std::uint16_t bus, std::uint8_t device, std::uint8_t func,
                            std::uint8_t reg);

void KeFastWritePCI16(std::uint16_t segment, std::uint16_t bus, std::uint8_t device, std::uint8_t func,
                      std::uint8_t reg, std::uint16_t value);
std::uint16_t KeFastReadPCI16(std::uint16_t segment, std::uint16_t bus, std::uint8_t device, std::uint8_t func,
                              std::uint8_t reg);

void KeFastWritePCI32(std::uint16_t segment, std::uint16_t bus, std::uint8_t device, std::uint8_t func,
                      std::uint8_t reg, std::uint32_t value);
std::uint32_t KeFastReadPCI32(std::uint16_t segment, std::uint16_t bus, std::uint8_t device, std::uint8_t func,
                              std::uint8_t reg);

void KeFastWritePCI64(std::uint16_t segment, std::uint16_t bus, std::uint8_t device, std::uint8_t func,
                      std::uint8_t reg, std::uint64_t value);
std::uint64_t KeFastReadPCI64(std::uint16_t segment, std::uint16_t bus, std::uint8_t device, std::uint8_t func,
                              std::uint8_t reg);

inline std::uint32_t PCIDevice::ReadConfig32(std::size_t offset) const
{
     return KeFastReadPCI32(segment, bus, device, function, static_cast<std::uint8_t>(offset));
}
inline void PCIDevice::WriteConfig32(std::size_t offset, std::uint32_t value) const
{
     KeFastWritePCI32(segment, bus, device, function, static_cast<std::uint8_t>(offset), value);
}
