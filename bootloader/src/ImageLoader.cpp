#include "ImageLoader.h"

#include <utils/PE.h>
#include <algorithm>
#include <memory>
#include <new>

inline CHAR16* operator""_16(const wchar_t* string, [[maybe_unused]] const std::size_t length)
{
     return const_cast<CHAR16*>( // NOLINT(cppcoreguidelines-pro-type-const-cast)
         reinterpret_cast<const CHAR16*>(string));
}

namespace bootloader
{
     ImageLoader::ImageLoader(EFI_BOOT_SERVICES* bootServices)
         : _bootServices(bootServices), _lastStatus(EFI_SUCCESS), _baseAddress(nullptr), _entryPoint(nullptr),
           _imageSize(0), _videoBaseAddress(nullptr), _videoImageSize(0)
     {
     }

     bool ImageLoader::ValidateImage(std::span<const std::byte> imageData)
     {
          if (imageData.size() < sizeof(DosHeader)) return false;

          const DosHeader& dosHeader = *std::launder(reinterpret_cast<const DosHeader*>(imageData.data()));
          if (dosHeader.eMagic != 0x5a4d) return false;

          if (imageData.size() < static_cast<std::size_t>(dosHeader.eLfanew) + sizeof(NtHeaders)) return false;

          const NtHeaders& ntHeader =
              *std::launder(reinterpret_cast<const NtHeaders*>(imageData.data() + dosHeader.eLfanew));
          return ntHeader.signature == 0x4550;
     }

     void ImageLoader::CopyHeaders(std::span<const std::byte> imageData, void* imageBase, std::uint32_t headerSize)
     {
          auto* dest = static_cast<std::byte*>(imageBase);
          for (std::uint32_t i = 0; i < headerSize; ++i) { dest[i] = imageData[i]; }
     }

     void ImageLoader::MapSections(std::span<const std::byte> imageData, void* imageBase, const SectionHeader* sections,
                                   std::uint16_t sectionCount)
     {
          for (std::uint16_t i = 0; i < sectionCount; ++i)
          {
               const SectionHeader& section = sections[i];

               if (section.sizeOfRawData == 0) continue;

               std::byte* dest = static_cast<std::byte*>(imageBase) + section.virtualAddress;
               const std::byte* src = imageData.data() + section.pointerToRawData;

               std::copy_n(src, section.sizeOfRawData, dest);
          }
     }

     void ImageLoader::ApplyRelocations(void* imageBase, const NtHeaders& ntHeader, std::uint64_t delta)
     {
          const DataDirectory& relocDir = ntHeader.optionalHeader.dataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
          if (relocDir.virtualAddress == 0 || relocDir.size == 0) return;

          auto* reloc = reinterpret_cast<std::byte*>(imageBase) + relocDir.virtualAddress;
          auto* end = reloc + relocDir.size;

          while (reloc < end)
          {
               auto* relocation = reinterpret_cast<ImageBaseRelocation*>(reloc);

               if (relocation->sizeOfBlock == 0) break; // safety guard

               std::uint8_t* dest = static_cast<std::uint8_t*>(imageBase) + relocation->virtualAddress;
               std::uint16_t* relocInfo = reinterpret_cast<std::uint16_t*>(reinterpret_cast<std::byte*>(relocation) +
                                                                           sizeof(ImageBaseRelocation));

               std::uint32_t numRelocations =
                   (relocation->sizeOfBlock - sizeof(ImageBaseRelocation)) / sizeof(std::uint16_t);

               for (std::uint32_t i = 0; i < numRelocations; ++i)
               {
                    std::uint16_t type = relocInfo[i] >> 12;
                    std::uint16_t offset = relocInfo[i] & 0xFFF;

                    if (type == IMAGE_REL_BASED_DIR64)
                    {
                         std::uint64_t* patchAddr = reinterpret_cast<std::uint64_t*>(dest + offset);
                         *patchAddr += delta;
                    }
               }

               reloc += relocation->sizeOfBlock;
          }
     }

     void* ImageLoader::LoadBootVideo(std::span<const std::byte> imageData)
     {
          const DosHeader& dosHeader = *reinterpret_cast<const DosHeader*>(imageData.data());
          const NtHeaders& ntHeader = *reinterpret_cast<const NtHeaders*>(imageData.data() + dosHeader.eLfanew);

          void* imageBase = reinterpret_cast<void*>(ntHeader.optionalHeader.imageBase);
          const std::size_t imageSize = ntHeader.optionalHeader.sizeOfImage;

          this->_lastStatus = this->_bootServices->AllocatePages(AllocateAnyPages, EfiLoaderCode, imageSize / 4096,
                                                                 reinterpret_cast<EFI_PHYSICAL_ADDRESS*>(&imageBase));

          std::fill_n(static_cast<std::byte*>(imageBase), imageSize, std::byte{0});

          std::copy_n(imageData.data(), ntHeader.optionalHeader.sizeOfHeaders, static_cast<std::byte*>(imageBase));

          const auto* sectionHeaders =
              reinterpret_cast<const SectionHeader*>(imageData.data() + dosHeader.eLfanew + sizeof(std::uint32_t) +
                                                     sizeof(CoffFileHeader) + ntHeader.fileHeader.sizeOfOptionalHeader);
          MapSections(imageData, imageBase, sectionHeaders, ntHeader.fileHeader.numberOfSections);

          std::uint64_t delta = reinterpret_cast<std::uint64_t>(imageBase) - ntHeader.optionalHeader.imageBase;
          if (delta) ApplyRelocations(imageBase, ntHeader, delta);
          this->_videoBaseAddress = imageBase;
          this->_videoImageSize = imageSize;

          return imageBase;
     }

     static void* ResolveExport(void* moduleBase, const char* name, std::uint16_t /*ordinal*/)
     {
          if (!moduleBase || !name) return nullptr;

          const auto* dos = static_cast<const DosHeader*>(moduleBase);
          const auto* nt = reinterpret_cast<const NtHeaders*>(static_cast<const std::byte*>(moduleBase) + dos->eLfanew);

          const DataDirectory& exportDir = nt->optionalHeader.dataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];

          if (exportDir.virtualAddress == 0) return nullptr;

          const auto* exports = reinterpret_cast<const ImageExportDirectory*>(
              static_cast<const std::byte*>(moduleBase) + exportDir.virtualAddress);

          const auto* funcRvas = reinterpret_cast<const std::uint32_t*>(static_cast<const std::byte*>(moduleBase) +
                                                                        exports->addressOfFunctions);
          const auto* names = reinterpret_cast<const std::uint32_t*>(static_cast<const std::byte*>(moduleBase) +
                                                                     exports->addressOfNames);
          const auto* ordinals = reinterpret_cast<const std::uint16_t*>(static_cast<const std::byte*>(moduleBase) +
                                                                        exports->addressOfNameOrdinals);

          for (std::uint32_t i = 0; i < exports->numberOfNames; ++i)
          {
               const char* exportName =
                   reinterpret_cast<const char*>(static_cast<const std::byte*>(moduleBase) + names[i]);

               if (std::ranges::equal(std::string_view(exportName), std::string_view(name)))
               {
                    std::uint16_t funcIndex = ordinals[i];
                    std::uint32_t rva = funcRvas[funcIndex];
                    return static_cast<std::byte*>(moduleBase) + rva;
               }
          }

          return nullptr; // not found
     }
     void ImageLoader::ResolveBootVideoImports(void* imageBase, const NtHeaders& ntHeader, void* bootVideoBase)
     {
          const DataDirectory& importDir = ntHeader.optionalHeader.dataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];

          if (importDir.virtualAddress == 0) return;

          auto* desc =
              reinterpret_cast<ImageImportDescriptor*>(static_cast<std::byte*>(imageBase) + importDir.virtualAddress);

          while (desc->name)
          {
               const char* moduleName = reinterpret_cast<const char*>(static_cast<std::byte*>(imageBase) + desc->name);

               if (!std::ranges::equal(std::string_view(moduleName), std::string_view("BootVideo.dll")))
               {
                    ++desc;
                    continue;
               }

               auto* thunk = reinterpret_cast<std::uintptr_t*>(static_cast<std::byte*>(imageBase) + desc->firstThunk);

               auto* origThunk =
                   reinterpret_cast<std::uintptr_t*>(static_cast<std::byte*>(imageBase) + desc->characteristics);

               if (!origThunk) origThunk = thunk;

               for (; *origThunk; ++origThunk, ++thunk)
               {
                    auto* import =
                        reinterpret_cast<ImageImportByName*>(static_cast<std::byte*>(imageBase) + *origThunk);

                    const char* name = reinterpret_cast<const char*>(import->name);

                    void* addr = ResolveExport(bootVideoBase, name, 0);
                    *thunk = reinterpret_cast<std::uintptr_t>(addr);
               }

               break;
          }
     }

     void* ImageLoader::LoadImage(std::span<const std::byte> imageData, EFI_STATUS* outStatus, void* bootVideoBase)
     {
          if (!ValidateImage(imageData))
          {
               this->_lastStatus = EFI_INVALID_PARAMETER;
               if (outStatus) *outStatus = this->_lastStatus;
               return nullptr;
          }

          const DosHeader& dosHeader = *std::launder(reinterpret_cast<const DosHeader*>(imageData.data()));
          const NtHeaders& ntHeader =
              *std::launder(reinterpret_cast<const NtHeaders*>(imageData.data() + dosHeader.eLfanew));

          void* imageBase = reinterpret_cast<void*>(ntHeader.optionalHeader.imageBase);
          UINTN imageSize = ntHeader.optionalHeader.sizeOfImage;
          UINTN pages = (imageSize + 0xFFF) / 0x1000;

          this->_lastStatus = this->_bootServices->AllocatePages(AllocateAnyPages, EfiLoaderCode, pages,
                                                                 reinterpret_cast<EFI_PHYSICAL_ADDRESS*>(&imageBase));
          if (EFI_ERROR(this->_lastStatus))
          {
               if (outStatus) *outStatus = _lastStatus;
               return nullptr;
          }

          std::uninitialized_fill_n(static_cast<std::byte*>(imageBase), imageSize, std::byte{0});

          CopyHeaders(imageData, imageBase, ntHeader.optionalHeader.sizeOfHeaders);

          const auto* sectionHeaders =
              reinterpret_cast<const SectionHeader*>(imageData.data() + dosHeader.eLfanew + sizeof(std::uint32_t) +
                                                     sizeof(CoffFileHeader) + ntHeader.fileHeader.sizeOfOptionalHeader);

          MapSections(imageData, imageBase, sectionHeaders, ntHeader.fileHeader.numberOfSections);

          std::uint64_t delta = reinterpret_cast<std::uint64_t>(imageBase) - ntHeader.optionalHeader.imageBase;
          if (delta != 0) ApplyRelocations(imageBase, ntHeader, delta);

          ResolveBootVideoImports(imageBase, ntHeader, bootVideoBase);

          this->_baseAddress = imageBase;
          this->_imageSize = imageSize;
          this->_entryPoint = static_cast<std::byte*>(imageBase) + ntHeader.optionalHeader.addressOfEntryPoint;

          this->_lastStatus = EFI_SUCCESS;
          if (outStatus) *outStatus = this->_lastStatus;

          return imageBase;
     }

     void ImageLoader::RelocateToVirtual(std::uintptr_t virtualBase)
     {
          if (!this->_baseAddress) return;
          const DosHeader& dosHeader = *static_cast<const DosHeader*>(this->_baseAddress);
          const NtHeaders& ntHeader =
              *reinterpret_cast<const NtHeaders*>(static_cast<std::byte*>(this->_baseAddress) + dosHeader.eLfanew);

          std::uintptr_t physicalBase = reinterpret_cast<std::uintptr_t>(this->_baseAddress);
          std::int64_t delta = static_cast<std::int64_t>(virtualBase) - static_cast<std::int64_t>(physicalBase);

          const DataDirectory& relocDir = ntHeader.optionalHeader.dataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];

          if (relocDir.virtualAddress != 0 && relocDir.size != 0)
          {
               auto* reloc = reinterpret_cast<std::byte*>(this->_baseAddress) + relocDir.virtualAddress;
               auto* end = reloc + relocDir.size;

               while (reloc != end)
               {
                    auto* relocation = reinterpret_cast<ImageBaseRelocation*>(reloc);

                    std::uint8_t* dest = static_cast<std::uint8_t*>(this->_baseAddress) + relocation->virtualAddress;
                    std::uint16_t* relocInfo = reinterpret_cast<std::uint16_t*>(
                        reinterpret_cast<std::byte*>(relocation) + sizeof(ImageBaseRelocation));

                    std::uint32_t numRelocations =
                        (relocation->sizeOfBlock - sizeof(ImageBaseRelocation)) / sizeof(std::uint16_t);

                    for (std::uint32_t i = 0; i < numRelocations; ++i)
                    {
                         std::uint16_t type = relocInfo[i] >> 12;
                         std::uint16_t offset = relocInfo[i] & 0xFFF;

                         if (type == IMAGE_REL_BASED_DIR64)
                         {
                              std::uint64_t* patchAddr = reinterpret_cast<std::uint64_t*>(dest + offset);
                              *patchAddr += delta;
                         }
                         else if (type == IMAGE_REL_BASED_HIGHLOW) // 32-bit relocation
                         {
                              std::uint32_t* patchAddr = reinterpret_cast<std::uint32_t*>(dest + offset);
                              *patchAddr += static_cast<std::uint32_t>(delta);
                         }
                    }

                    reloc += relocation->sizeOfBlock;
               }
          }

          std::uintptr_t entryOffset = ntHeader.optionalHeader.addressOfEntryPoint;
          this->_entryPoint = reinterpret_cast<void*>(virtualBase + entryOffset);
     }

} // namespace bootloader
