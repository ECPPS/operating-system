#pragma once
#include <array>

#include <Uefi.h>

#include <Protocol/GraphicsOutput.h>

#include <utils/arch.h>
#include <utils/memory.h>

namespace bootloader
{
     class BootContext
     {
     public:
          BootContext(EFI_HANDLE imageHandle, EFI_SYSTEM_TABLE* systemTable);

          bool PrepareLoaderBlock(arch::LoaderParameterBlock* outBlock);
          bool FinaliseLoaderBlock(arch::LoaderParameterBlock* outBlock);

          bool SetupPageTables(void* imageBase, std::size_t imageSize, std::uintptr_t stackPointer,
                               const arch::Framebuffer& framebuffer);
          bool RemapKernelVirtual(void* physicalBase, std::size_t imageSize, std::uintptr_t virtualBase,
                                  void* videoDllPhysicalBase, std::size_t videoDllImageSize,
                                  std::uintptr_t videoDllVirtualBase);
          bool MapKernelStack(std::uintptr_t physicalBase, std::size_t stackSize, std::uintptr_t virtualBase);
          bool ExitBootServices(UINTN* outMapKey);
          static void ReclaimBootServices(arch::LoaderParameterBlock* loaderBlock);
          [[nodiscard]] EFI_STATUS GetLastStatus() const { return _lastStatus; }
          [[nodiscard]] std::uintptr_t GetPageTableRoot() const { return _pageTableRoot; }
          [[nodiscard]] EFI_BOOT_SERVICES* GetBootServices() const { return _bootServices; }
          [[nodiscard]] EFI_SYSTEM_TABLE* GetSystemTable() const { return _systemTable; }

     private:
          struct MappedRegion
          {
               std::uintptr_t start;
               std::size_t size;
               const wchar_t* name;
          };

          EFI_HANDLE _imageHandle;
          EFI_SYSTEM_TABLE* _systemTable;
          EFI_BOOT_SERVICES* _bootServices;
          EFI_STATUS _lastStatus;
          std::uintptr_t _pageTableRoot;
          std::uintptr_t _maxPhysicalAddress;

          MappedRegion* _mappedRegions;
          std::size_t _mappedRegionCount;
          std::size_t _mappedRegionCapacity;

          EFI_STATUS GetFramebufferInfo(arch::Framebuffer* outFramebuffer);
          EFI_STATUS BuildMemoryDescriptors(arch::LoaderParameterBlock* outBlock);
          bool MapRegion(std::uintptr_t physicalAddress, std::size_t size, bool writable, const wchar_t* name);
          bool MapEfiRuntimeRegions();
          void TrackMappedRegion(std::uintptr_t start, std::size_t size, const wchar_t* name);

          static void* AllocatePageTableMemory(std::size_t size);

          static EFI_BOOT_SERVICES* s_bootServices;
     };

} // namespace bootloader
