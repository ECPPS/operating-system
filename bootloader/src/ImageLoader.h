#pragma once

#include <cstddef>
#include <span>

#include <Uefi.h>
#include <utils/PE.h>

namespace bootloader
{
     class ImageLoader
     {
     public:
          explicit ImageLoader(EFI_BOOT_SERVICES* bootServices);

          bool ValidateImage(std::span<const std::byte> imageData);

          void* LoadImage(std::span<const std::byte> imageData, EFI_STATUS* outStatus, void* bootVideoBase);

          void RelocateToVirtual(std::uintptr_t virtualBase);

          [[nodiscard]] void* GetEntryPoint() const noexcept { return this->_entryPoint; }
          [[nodiscard]] void* GetBaseAddress() const noexcept { return this->_baseAddress; }
          [[nodiscard]] std::size_t GetImageSize() const noexcept { return this->_imageSize; }
          [[nodiscard]] EFI_STATUS GetLastStatus() const noexcept { return this->_lastStatus; }
          [[nodiscard]] void* GetVideoBaseAddress() const noexcept { return this->_videoBaseAddress; }
          [[nodiscard]] std::size_t GetVideoImageSize() const noexcept { return this->_videoImageSize; }
          void* LoadBootVideo(std::span<const std::byte> imageData);

     private:
          EFI_BOOT_SERVICES* _bootServices;
          EFI_STATUS _lastStatus;
          void* _baseAddress;
          void* _entryPoint;
          std::size_t _imageSize;
          void* _videoBaseAddress;
          std::size_t _videoImageSize;

          void ResolveBootVideoImports(void* imageBase, const NtHeaders& ntHeader, void* bootVideoBase);
          void CopyHeaders(std::span<const std::byte> imageData, void* imageBase, std::uint32_t headerSize);
          void MapSections(std::span<const std::byte> imageData, void* imageBase, const SectionHeader* sections,
                           std::uint16_t sectionCount);
          void ApplyRelocations(void* imageBase, const NtHeaders& ntHeader, std::uint64_t delta);
     };

} // namespace bootloader
