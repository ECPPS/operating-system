#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include <Uefi.h>

#include <Guid/Acpi.h>
#include <Guid/FileInfo.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/SimpleFileSystem.h>

#include <utils/arch.h>
#include <utils/identify.h>
#include <utils/memory.h>

#include "BootContext.h"
#include "FileLoader.h"
#include "ImageLoader.h"

EFI_GUID gEfiSimpleFileSystemProtocolGuid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
EFI_GUID gEfiFileInfoGuid = EFI_FILE_INFO_ID;
EFI_GUID gEfiGraphicsOutputProtocolGuid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
EFI_GUID gEfiAcpi20TableGuid = ACPI_10_TABLE_GUID;
EFI_GUID gEfiAcpi10TableGuid = EFI_ACPI_TABLE_GUID;

static BOOLEAN CompareGuid(const EFI_GUID* Guid1, const EFI_GUID* Guid2)
{
     for (UINTN i = 0; i < sizeof(EFI_GUID); i++)
     {
          if ((reinterpret_cast<const std::byte*>(Guid1))[i] != (reinterpret_cast<const std::byte*>(Guid2))[i])
               return FALSE;
     }
     return TRUE;
}

static EFI_PHYSICAL_ADDRESS GetAcpiRsdpAddress(EFI_SYSTEM_TABLE* SystemTable)
{
     for (UINTN i = 0; i < SystemTable->NumberOfTableEntries; i++)
     {
          if (CompareGuid(&SystemTable->ConfigurationTable[i].VendorGuid, &gEfiAcpi20TableGuid))
          {
               return reinterpret_cast<EFI_PHYSICAL_ADDRESS>(SystemTable->ConfigurationTable[i].VendorTable);
          }
          if (CompareGuid(&SystemTable->ConfigurationTable[i].VendorGuid, &gEfiAcpi10TableGuid))
          {
               return reinterpret_cast<EFI_PHYSICAL_ADDRESS>(SystemTable->ConfigurationTable[i].VendorTable);
          }
     }

     return 0; // :(
}

inline CHAR16* operator""_16(const wchar_t* string, [[maybe_unused]] const std::size_t length)
{
     return const_cast<CHAR16*>( // NOLINT(cppcoreguidelines-pro-type-const-cast)
         reinterpret_cast<const CHAR16*>(string));
}

extern "C" [[noreturn]] void SwitchStackAndCall(std::uintptr_t newStack, void* parameter, void (*function)(void*));

namespace
{
     std::uintptr_t GetStackPointer()
     {
#if defined(ARCH_X8664)
#ifdef COMPILER_MSVC
          return reinterpret_cast<std::uintptr_t>(_AddressOfReturnAddress());
#else
          std::uintptr_t sp = 0;
          asm volatile("mov %%rsp, %0" : "=r"(sp));
          return sp;
#endif
#elif defined(ARCH_X8632)
#ifdef COMPILER_MSVC
          return reinterpret_cast<std::uintptr_t>(_AddressOfReturnAddress());
#else
          std::uintptr_t sp = 0;
          asm volatile("mov %%esp, %0" : "=r"(sp));
          return sp;
#endif
#elif defined(ARCH_ARM64)
#ifdef COMPILER_MSVC
          return reinterpret_cast<std::uintptr_t>(_AddressOfReturnAddress());
#else
          std::uintptr_t sp = 0;
          asm volatile("mov %0, sp" : "=r"(sp));
          return sp;
#endif
#else
#error "Unsupported architecture"
#endif
     }

     void WaitForKeyPress(EFI_SYSTEM_TABLE* systemTable)
     {
          EFI_INPUT_KEY key;
          systemTable->ConIn->Reset(systemTable->ConIn, FALSE);

          UINTN index{};
          systemTable->BootServices->WaitForEvent(1, &systemTable->ConIn->WaitForKey, &index);
          systemTable->ConIn->ReadKeyStroke(systemTable->ConIn, &key);
     }

     void PrintStatus(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* conOut, EFI_STATUS status)
     {
          CHAR16 hexBuffer[19]{}; // NOLINT(modernize-avoid-c-arrays, cppcoreguidelines-avoid-c-arrays)
          hexBuffer[0] = L'0';
          hexBuffer[1] = L'x';

          UINTN value = static_cast<UINTN>(status);
          for (int i = 17; i >= 2; --i)
          {
               UINTN digit = value & 0xF;
               hexBuffer[i] = digit < 10 ? L'0' + digit : L'A' + (digit - 10);
               value >>= 4;
          }
          hexBuffer[18] = L'\0';

          conOut->OutputString(conOut, hexBuffer);
     }
} // namespace

EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* conOut;

static std::uint64_t UefiTimeToUnix(const EFI_TIME& t)
{
     constexpr std::array<std::uint16_t, 12> daysBeforeMonth = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};

     auto isLeap = [](std::uint32_t y) { return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0); };

     std::uint32_t year = t.Year;
     std::uint64_t days = 0;
     for (std::uint32_t y = 1970; y < year; y++) days += isLeap(y) ? 366 : 365;

     days += daysBeforeMonth[t.Month - 1];
     if (t.Month > 2 && isLeap(year)) days++;
     days += t.Day - 1;

     return (days * 86400) + (t.Hour * 3600uz) + (t.Minute * 60uz) + t.Second;
}

extern "C" EFI_STATUS EFIAPI EfiMain(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE* SystemTable)
{
     conOut = SystemTable->ConOut;

     conOut->ClearScreen(conOut);
     conOut->OutputString(conOut, L"Hi "_16);
     conOut->OutputString(conOut, operator""_16(WConfiguration.data(), WConfiguration.size()));
     conOut->OutputString(conOut, L"\r\n"_16);

     bootloader::BootContext bootContext(ImageHandle, SystemTable);

     bootloader::FileLoader fileLoader(bootContext.GetBootServices());

     conOut->OutputString(conOut, L"Loading kernel...\r\n"_16);
     const std::basic_string_view<CHAR16> kernelPath = L"\\kernel\\kernel.exe"_16;
     const std::basic_string_view<CHAR16> bootvidPath = L"\\kernel\\BootVideo.dll"_16;

     EFI_STATUS status = EFI_SUCCESS;
     std::span<std::byte> kernelData = fileLoader.LoadFile(kernelPath, &status);

     if (EFI_ERROR(status) || kernelData.empty())
     {
          conOut->OutputString(conOut, L"Failed to load kernel file, status: "_16);
          PrintStatus(conOut, status);
          conOut->OutputString(conOut, L"\r\n"_16);
          WaitForKeyPress(SystemTable);
          return status;
     }

     std::span<std::byte> bootVideoData = fileLoader.LoadFile(bootvidPath, &status);

     if (EFI_ERROR(status) || bootVideoData.empty())
     {
          conOut->OutputString(conOut, L"Failed to load BootVideo.dll, status: "_16);
          PrintStatus(conOut, status);
          conOut->OutputString(conOut, L"\r\n"_16);
          WaitForKeyPress(SystemTable);
          return status;
     }

     bootloader::ImageLoader imageLoader(bootContext.GetBootServices());

     conOut->OutputString(conOut, L"Validating kernel image...\r\n"_16);
     if (!imageLoader.ValidateImage(kernelData))
     {
          conOut->OutputString(conOut, L"Failed to validate kernel image\r\n"_16);
          fileLoader.FreeFile(kernelData);
          WaitForKeyPress(SystemTable);
          return EFI_INVALID_PARAMETER;
     }
     if (!imageLoader.ValidateImage(bootVideoData))
     {
          conOut->OutputString(conOut, L"Failed to validate kernel image\r\n"_16);
          fileLoader.FreeFile(kernelData);
          WaitForKeyPress(SystemTable);
          return EFI_INVALID_PARAMETER;
     }

     void* bootVideoBase = imageLoader.LoadBootVideo(bootVideoData);

     conOut->OutputString(conOut, L"Loading and relocating kernel image...\r\n"_16);
     void* imageBase = imageLoader.LoadImage(kernelData, &status, bootVideoBase);

     fileLoader.FreeFile(kernelData);
     EFI_TIME uefiTime{};
     SystemTable->RuntimeServices->GetTime(&uefiTime, nullptr);

     if (EFI_ERROR(status) || !imageBase)
     {
          conOut->OutputString(conOut, L"Failed to load kernel image, status: "_16);
          PrintStatus(conOut, status);
          conOut->OutputString(conOut, L"\r\n"_16);
          WaitForKeyPress(SystemTable);
          return status;
     }

     conOut->OutputString(conOut, L"Preparing loader parameter block...\r\n"_16);
     static arch::LoaderParameterBlock loaderBlock{};
     loaderBlock.systemMajor = OsVersionMajor;
     loaderBlock.systemMinor = OsVersionMinor;
     loaderBlock.size = sizeof(arch::LoaderParameterBlock);

     if (!bootContext.PrepareLoaderBlock(&loaderBlock))
     {
          conOut->OutputString(conOut, L"Failed to prepare loader block, status: "_16);
          PrintStatus(conOut, bootContext.GetLastStatus());
          conOut->OutputString(conOut, L"\r\n"_16);
          WaitForKeyPress(SystemTable);
          return bootContext.GetLastStatus();
     }
     const auto acpiPhysical = GetAcpiRsdpAddress(SystemTable);

     conOut->OutputString(conOut, L"Allocating kernel stack...\r\n"_16);
     constexpr UINTN stackPages = KernelStackSize / 0x1000;
     EFI_PHYSICAL_ADDRESS stackPhysical = 0;
     status = bootContext.GetBootServices()->AllocatePages(AllocateAnyPages, EfiLoaderData, stackPages, &stackPhysical);
     if (EFI_ERROR(status))
     {
          conOut->OutputString(conOut, L"Failed to allocate kernel stack, status: "_16);
          PrintStatus(conOut, status);
          conOut->OutputString(conOut, L"\r\n"_16);
          WaitForKeyPress(SystemTable);
          return status;
     }

     conOut->OutputString(conOut, L"Setting up page tables...\r\n"_16);
     std::uintptr_t stackPointer = GetStackPointer();
     if (!bootContext.SetupPageTables(imageBase, imageLoader.GetImageSize(), stackPointer, loaderBlock.framebuffer))
     {
          conOut->OutputString(conOut, L"Failed to setup page tables, status: "_16);
          PrintStatus(conOut, bootContext.GetLastStatus());
          conOut->OutputString(conOut, L"\r\n"_16);
          WaitForKeyPress(SystemTable);
          return bootContext.GetLastStatus();
     }

     conOut->OutputString(conOut, L"Remapping kernel to virtual address...\r\n"_16);
     if (!bootContext.RemapKernelVirtual(imageBase, imageLoader.GetImageSize(), KernelVirtualBase,
                                         imageLoader.GetVideoBaseAddress(), imageLoader.GetVideoImageSize(),
                                         VideoVirtualBase))
     {
          conOut->OutputString(conOut, L"Failed to remap kernel, status: "_16);
          PrintStatus(conOut, bootContext.GetLastStatus());
          conOut->OutputString(conOut, L"\r\n"_16);
          WaitForKeyPress(SystemTable);
          return bootContext.GetLastStatus();
     }

     conOut->OutputString(conOut, L"Relocating kernel to virtual addresses...\r\n"_16);
     imageLoader.RelocateToVirtual(KernelVirtualBase);

     conOut->OutputString(conOut, L"Mapping kernel stack to virtual address...\r\n"_16);
     if (!bootContext.MapKernelStack(stackPhysical, KernelStackSize, KernelStackVirtualBase))
     {
          conOut->OutputString(conOut, L"Failed to map kernel stack, status: "_16);
          PrintStatus(conOut, bootContext.GetLastStatus());
          conOut->OutputString(conOut, L"\r\n"_16);
          WaitForKeyPress(SystemTable);
          return bootContext.GetLastStatus();
     }

     conOut->OutputString(conOut, L"Finalising loader block with memory descriptors...\r\n"_16);
     loaderBlock.memoryDescriptors.head = nullptr;
     if (!bootContext.FinaliseLoaderBlock(&loaderBlock))
     {
          conOut->OutputString(conOut, L"Failed to finalise loader block, status: "_16);
          PrintStatus(conOut, bootContext.GetLastStatus());
          conOut->OutputString(conOut, L"\r\n"_16);
          WaitForKeyPress(SystemTable);
          return bootContext.GetLastStatus();
     }

     loaderBlock.kernelVirtualBase = KernelVirtualBase;
     loaderBlock.bootVideoVirtualBase = VideoVirtualBase;
     loaderBlock.kernelPhysicalBase = reinterpret_cast<std::uintptr_t>(imageBase);
     loaderBlock.kernelSize = imageLoader.GetImageSize();
     loaderBlock.bootVideoImageSize = imageLoader.GetVideoImageSize();
     loaderBlock.stackVirtualBase = KernelStackVirtualBase;
     loaderBlock.stackSize = KernelStackSize;
     loaderBlock.acpiPhysical = acpiPhysical;
     loaderBlock.bootTimeSeconds = UefiTimeToUnix(uefiTime);

     conOut->OutputString(conOut, L"Exiting boot services...\r\n"_16);
     UINTN mapKey = 0;
     if (!bootContext.ExitBootServices(&mapKey))
     {
          conOut->OutputString(conOut, L"Failed to exit boot services, status: "_16);
          PrintStatus(conOut, bootContext.GetLastStatus());
          conOut->OutputString(conOut, L"\r\n"_16);
          WaitForKeyPress(SystemTable);
          return bootContext.GetLastStatus();
     }

     bootloader::BootContext::ReclaimBootServices(&loaderBlock);

     constexpr std::uintptr_t DirectMapOffset = 0xFFFF800000000000ULL;

     if (loaderBlock.memoryDescriptors.head != nullptr)
     {
          structures::ListEntry<arch::MemoryDescriptor>* current = loaderBlock.memoryDescriptors.head;
          loaderBlock.memoryDescriptors.head = reinterpret_cast<structures::ListEntry<arch::MemoryDescriptor>*>(
              reinterpret_cast<std::uintptr_t>(loaderBlock.memoryDescriptors.head) + DirectMapOffset);

          while (current != nullptr)
          {
               structures::ListEntry<arch::MemoryDescriptor>* next = current->next;

               if (current->next != nullptr)
                    current->next = reinterpret_cast<structures::ListEntry<arch::MemoryDescriptor>*>(
                        reinterpret_cast<std::uintptr_t>(current->next) + DirectMapOffset);

               if (current->previous != nullptr)
                    current->previous = reinterpret_cast<structures::ListEntry<arch::MemoryDescriptor>*>(
                        reinterpret_cast<std::uintptr_t>(current->previous) + DirectMapOffset);

               current = next;
          }
     }

     memory::paging::LoadPageTable(bootContext.GetPageTableRoot());
     memory::paging::EnablePaging();

     void* entryPoint = imageLoader.GetEntryPoint();
     std::uintptr_t newStackTop = KernelStackVirtualBase + KernelStackSize;

     using EntryPointFunc = void (*)(void*);
     SwitchStackAndCall(newStackTop, &loaderBlock, reinterpret_cast<EntryPointFunc>(entryPoint)); // noreturn
}

extern "C" void KeHandleInterruptFrame() // a stub in case something went horribly wrong lol
{
     while (true);
}
