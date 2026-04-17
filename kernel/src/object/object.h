#pragma once

#include "objecttypes.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace kernel
{
     class ProcessControlBlock;
}

namespace object
{
     inline constexpr std::size_t kMaxHandlesPerProcess = 1024;
     inline constexpr std::size_t kGlobalDirectoryBuckets = 128;
     inline constexpr std::uint32_t kMaxSymlinkDepth = 32;

     static_assert((kMaxHandlesPerProcess & (kMaxHandlesPerProcess - 1)) == 0);
     static_assert((kGlobalDirectoryBuckets & (kGlobalDirectoryBuckets - 1)) == 0);

     class GlobalObjectDirectory
     {
     public:
          GlobalObjectDirectory() noexcept = default;

          GlobalObjectDirectory(const GlobalObjectDirectory&) = delete;
          GlobalObjectDirectory& operator=(const GlobalObjectDirectory&) = delete;
          GlobalObjectDirectory(GlobalObjectDirectory&&) = delete;
          GlobalObjectDirectory& operator=(GlobalObjectDirectory&&) = delete;

          [[nodiscard]] bool Insert(ObjectHeader* header) noexcept;

          void Remove(ObjectHeader* header) noexcept;

          [[nodiscard]] ObjectHeader* Find(std::string_view name) noexcept;
          [[nodiscard]] const ObjectHeader* Find(std::string_view name) const noexcept;

          using EnumerateCallback = void (*)(std::size_t bucket, const ObjectHeader* header, void* context) noexcept;
          void Enumerate(EnumerateCallback callback, void* context) const noexcept;

     private:
          [[nodiscard]] static std::size_t Hash(std::string_view name) noexcept;

          std::array<ObjectHeader*, kGlobalDirectoryBuckets> _buckets{};
     };

     extern GlobalObjectDirectory g_directory;

     extern Handle g_rootDirectoryHandle;

     extern Handle g_globalRootHandle;

     extern ObjectHeader* g_rootDirectoryHeader;
     extern ObjectHeader* g_globalRootHeader;

     struct HandleTableEntry
     {
          ObjectHeader* header{nullptr};
          AccessRights access{AccessRights::None};
          std::uint32_t generation{};
     };

     class HandleTable
     {
     public:
          HandleTable() noexcept;

          HandleTable(const HandleTable&) = delete;
          HandleTable& operator=(const HandleTable&) = delete;
          HandleTable(HandleTable&&) = delete;
          HandleTable& operator=(HandleTable&&) = delete;

          [[nodiscard]] Handle AllocateSlot(ObjectHeader* header, AccessRights access) noexcept;

          ObjectStatus FreeSlot(Handle handle) noexcept;

          [[nodiscard]] HandleTableEntry* LookupEntry(Handle handle) noexcept;
          [[nodiscard]] const HandleTableEntry* LookupEntry(Handle handle) const noexcept;

          [[nodiscard]] ObjectHeader* LookupHeader(Handle handle) noexcept;

          [[nodiscard]] std::size_t ActiveHandleCount() const noexcept { return _activeCount; }
          [[nodiscard]] bool IsFull() const noexcept { return _freeCount == 0; }

     private:
          std::array<HandleTableEntry, kMaxHandlesPerProcess> _slots{};
          std::array<std::uint16_t, kMaxHandlesPerProcess> _freeStack{};
          std::size_t _freeTop{};
          std::size_t _freeCount{};
          std::size_t _activeCount{};
     };
     struct ObjectAttributes
     {
          std::string_view name{}; // "" == anonymous
          ObjectType type{ObjectType::Unknown};
          std::uint32_t bodySize{};
          ObjectDestructor destructor{nullptr};
          AccessRights desiredAccess{AccessRights::All};
     };

     [[nodiscard]] ObjectHeader* OiAllocateObjectMemory(std::size_t bodySize) noexcept;
     void OiFreeObjectMemory(ObjectHeader* header) noexcept;

     void OiInitialiseHeader(ObjectHeader* header, std::string_view name, ObjectType type, std::uint32_t bodySize,
                             ObjectDestructor destructor, AccessRights access) noexcept;

     std::int32_t OiReferenceObject(ObjectHeader* header) noexcept;
     std::int32_t OiDereferenceObject(ObjectHeader* header) noexcept;

     [[nodiscard]] ObjectHeader* OiResolvePath(ObjectHeader* startDir, std::string_view path, OpenFlags flags,
                                               ObjectStatus* outStatus) noexcept;

     void OiRegisterTypeDescriptor(kernel::ProcessControlBlock& pcb, ObjectType type,
                                   std::string_view typeName) noexcept;

     [[nodiscard]] Handle ObInsertObject(kernel::ProcessControlBlock& pcb, ObjectHeader* parentDir,
                                         ObjectHeader* header, AccessRights desiredAccess) noexcept;

     [[nodiscard]] Handle ObCreateObject(kernel::ProcessControlBlock& pcb, const ObjectAttributes& attributes) noexcept;
     [[nodiscard]] Handle ObCreateObjectIn(kernel::ProcessControlBlock& pcb, const ObjectAttributes& attributes,
                                           std::string_view parentPath) noexcept;

     [[nodiscard]] Handle ObOpenObjectByPath(kernel::ProcessControlBlock& pcb, std::string_view path,
                                             AccessRights desiredAccess, OpenFlags flags = OpenFlags::None) noexcept;

     [[nodiscard]] Handle ObOpenObjectByAbsolutePath(kernel::ProcessControlBlock& pcb, std::string_view path,
                                                     AccessRights desiredAccess,
                                                     OpenFlags flags = OpenFlags::None) noexcept;

     [[nodiscard]] Handle ObOpenObjectByName(kernel::ProcessControlBlock& pcb, std::string_view name,
                                             AccessRights desiredAccess) noexcept;

     ObjectStatus ObCloseHandle(kernel::ProcessControlBlock& pcb, Handle handle) noexcept;

     [[nodiscard]] Handle ObDuplicateHandle(kernel::ProcessControlBlock& srcPcb, kernel::ProcessControlBlock& dstPcb,
                                            Handle sourceHandle, AccessRights desiredAccess) noexcept;

     [[nodiscard]] ObjectHeader* ObQueryObject(kernel::ProcessControlBlock& pcb, Handle handle) noexcept;

     template <typename T>
     [[nodiscard]] T* ObGetBody(kernel::ProcessControlBlock& pcb, Handle handle, ObjectType expectedType) noexcept;

     void KeRegisterProcessorObject(kernel::ProcessControlBlock& pcb, std::uint32_t logicalIndex,
                                    std::uint32_t apicId) noexcept;

     void KeInitialiseOb() noexcept;
} // namespace object
