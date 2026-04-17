#pragma once

#include <cstddef>
#include <cstdint>

namespace memory
{
     extern std::uintptr_t virtualOffset;

     struct PageTableEntry
     {
          std::uintptr_t physicalAddress{};
          bool present{};
          bool writable{};
          bool userAccessible{};
          bool writeThrough{};
          bool cacheDisable{};
          bool accessed{};
          bool dirty{};
          bool largePage{};
          bool global{};
     };

     enum struct CachePolicy : std::uint8_t
     {
          Uncacheable,
          WriteCombining,
          WriteThrough,
          WriteBack,
          WriteProtected,
     };

     struct PageMapping
     {
          std::uintptr_t virtualAddress{};
          std::uintptr_t physicalAddress{};
          std::size_t size{};
          bool writable{};
          bool userAccessible{};
          bool executable{};
          bool global{};
          CachePolicy cachePolicy = CachePolicy::WriteBack;
     };

     namespace paging
     {
          constexpr std::size_t GetPageSize() noexcept;
          std::uintptr_t CreatePageTable(void* (*allocator)(std::size_t));
          bool MapPage(std::uintptr_t pageTableRoot, const PageMapping& mapping, void* (*allocator)(std::size_t));
          bool UnmapPage(std::uintptr_t pageTableRoot, std::uintptr_t virtualAddress, bool invalidate = true);
          bool ProtectPage(std::uintptr_t pageTableRoot, std::uintptr_t virtualAddress, bool isWritable,
                           bool isUserAccessible, bool isExecutable, void* (*allocator)(std::size_t));

          bool MapPhysicalMemoryDirect(std::uintptr_t pageTableRoot, std::size_t maxPhysicalAddress,
                                       void* (*allocator)(std::size_t), std::size_t startOffset = 0);

          void LoadPageTable(std::uintptr_t pageTableRoot);
          std::uintptr_t GetCurrentPageTable();
          void InvalidatePage(std::uintptr_t virtualAddress);
          void FlushTLB();
          void EnablePaging();
     } // namespace paging
} // namespace memory
