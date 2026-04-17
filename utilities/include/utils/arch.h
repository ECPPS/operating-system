#pragma once

#include <cstddef>
#include <cstdint>
#include "struct.h"

namespace arch
{
     enum struct MemoryType : std::uint32_t // NOLINT
     {
          Free,
          Reserved,
          LoaderCode,
          LoaderData,
          BootServicesCode,
          BootServicesData,
          RuntimeServicesCode,
          RuntimeServicesData,
          UnusableMemory,
          ACPIReclaimMemory,
          ACPIMemoryNVS,
          MemoryMappedIO,
          MemoryMappedIOPortSpace,
          PalCode,
          PersistentMemory,
          Bad
     };
     struct MemoryDescriptor
     {
          MemoryType type{};
          std::uint64_t baseCount{};
          std::uintptr_t basePage{};
     };
     struct Framebuffer
     {
          std::uintptr_t physicalStart{};
          std::size_t totalSize{};
          std::uint32_t width{};
          std::uint32_t height{};
          std::uint32_t scanlineSize{};
          std::uint32_t maskRed{};
          std::uint32_t maskGreen{};
          std::uint32_t maskBlue{};
     };

     struct LoaderParameterBlock
     {
          std::uint16_t systemMajor{};
          std::uint16_t systemMinor{};
          std::uint16_t size{};
          std::uint16_t reserved{};
          structures::LinkedList<MemoryDescriptor> memoryDescriptors{};
          Framebuffer framebuffer{};
          std::uintptr_t kernelVirtualBase{};
          std::uintptr_t kernelPhysicalBase{};
          std::size_t kernelSize{};
          std::uintptr_t stackVirtualBase{};
          std::size_t stackSize{};
          std::uintptr_t bootVideoVirtualBase{};
          std::size_t bootVideoImageSize{};
          std::uintptr_t acpiPhysical{};
          std::uint64_t bootTimeSeconds{};
     };

     //
     // ABI assurance
     //

     // MemoryDescriptor
     static_assert(alignof(MemoryDescriptor) == alignof(std::uintptr_t));
     static_assert(sizeof(MemoryDescriptor) == 3 * sizeof(std::uint64_t));
     static_assert(alignof(structures::ListEntry<MemoryDescriptor>) == alignof(std::uintptr_t));
     static_assert(sizeof(structures::ListEntry<MemoryDescriptor>) == 5uz * sizeof(std::uintptr_t));

     // LoaderParameterBlock
     static_assert(alignof(LoaderParameterBlock) == alignof(std::uintptr_t));
     static_assert(sizeof(LoaderParameterBlock) ==
                   sizeof(std::uintptr_t) + 8 + sizeof(Framebuffer) + (9uz * sizeof(std::uintptr_t)));
} // namespace arch
