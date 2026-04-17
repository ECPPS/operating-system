#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

namespace object
{
     inline constexpr std::size_t kObjectNameCapacity = 64;

     struct ObjectName
     {
          std::array<char, kObjectNameCapacity> data{};

          constexpr ObjectName() noexcept = default;

          explicit ObjectName(std::string_view sv) noexcept
          {
               const std::size_t len = sv.size() < kObjectNameCapacity ? sv.size() : kObjectNameCapacity;
               for (std::size_t i = 0; i < len; ++i) data[i] = sv[i];
          }

          [[nodiscard]] constexpr std::string_view View() const noexcept
          {
               std::size_t len = 0;
               while (len < kObjectNameCapacity && data[len] != '\0') ++len;
               return {data.data(), len};
          }

          [[nodiscard]] bool operator==(const ObjectName& other) const noexcept { return data == other.data; }
     };

     enum struct ObjectType : std::uint16_t
     {
          Unknown = 0,
          Process = 1,
          Thread = 2,
          File = 3,
          Event = 4,
          Mutex = 5,
          Semaphore = 6,
          Timer = 7,
          Section = 8,
          Port = 9, // IPC
          SymbolicLink = 10,
          Directory = 11,
          Device = 12,
          Driver = 13,
          TypeDescriptor = 14, // \ObjectTypes
          Processor = 15,      // \Processor
          _Count = 16
     };

     struct HandleSentinel
     {
          std::uintptr_t opaque{};
     };
     using Handle = HandleSentinel*;

     inline constexpr Handle kInvalidHandle = nullptr;

     [[nodiscard]] inline Handle OiIndexToHandle(std::uintptr_t index) noexcept
     {
          return reinterpret_cast<Handle>(index + 1u);
     }

     [[nodiscard]] inline std::uintptr_t OiHandleToIndex(Handle handle) noexcept
     {
          return reinterpret_cast<std::uintptr_t>(handle) - 1u;
     }

     [[nodiscard]] inline bool OiIsValidHandle(Handle handle) noexcept { return handle != kInvalidHandle; }

     enum struct AccessRights : std::uint32_t
     {
          None = 0,
          Read = 1u << 0,
          Write = 1u << 1,
          Execute = 1u << 2,
          Delete = 1u << 3,
          Synchronise = 1u << 4,
          All = 0xFFFF'FFFFu,
     };

     [[nodiscard]] constexpr AccessRights operator|(AccessRights a, AccessRights b) noexcept
     {
          return static_cast<AccessRights>(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
     }
     [[nodiscard]] constexpr AccessRights operator&(AccessRights a, AccessRights b) noexcept
     {
          return static_cast<AccessRights>(static_cast<std::uint32_t>(a) & static_cast<std::uint32_t>(b));
     }
     [[nodiscard]] constexpr bool HasAccess(AccessRights actual, AccessRights required) noexcept
     {
          return (static_cast<std::uint32_t>(actual) & static_cast<std::uint32_t>(required)) ==
                 static_cast<std::uint32_t>(required);
     }

     enum struct OpenFlags : std::uint32_t
     {
          None = 0,
          NoFollow = 1u << 0,
          CreateDirs = 1u << 1
     };

     [[nodiscard]] constexpr OpenFlags operator|(OpenFlags a, OpenFlags b) noexcept
     {
          return static_cast<OpenFlags>(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
     }
     [[nodiscard]] constexpr bool HasFlag(OpenFlags flags, OpenFlags test) noexcept
     {
          return (static_cast<std::uint32_t>(flags) & static_cast<std::uint32_t>(test)) != 0;
     }

     enum struct ObjectStatus : std::int32_t
     {
          Success = 0,
          InvalidHandle = -1,
          InvalidType = -2,
          NameCollision = -3,
          TableFull = -4,
          AccessDenied = -5,
          ObjectNotFound = -6,
          InvalidParameter = -7,
          AlreadyExists = -8,
          SymbolicLinkLoop = -9,
          NotADirectory = -10,
          NotFound = -11,
          OutOfMemory = -12,
     };

     struct ObjectHeader;
     using ObjectDestructor = void (*)(void* body) noexcept;

     struct alignas(std::max_align_t) ObjectHeader
     {
          ObjectName accessName{};
          ObjectType type{};
          std::uint16_t pad0{};

          std::atomic<std::int32_t> referenceCount{1};
          ObjectDestructor destructor{nullptr};

          std::uint32_t bodySize{};

          AccessRights grantedAccess{AccessRights::All};

          ObjectHeader* nextInBucket{nullptr};

          ObjectHeader* parentDirectory{nullptr};

          [[nodiscard]] void* Body() noexcept
          {
               return reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(this) + sizeof(ObjectHeader));
          }
          [[nodiscard]] const void* Body() const noexcept
          {
               return reinterpret_cast<const void*>(reinterpret_cast<std::uintptr_t>(this) + sizeof(ObjectHeader));
          }

          template <typename T> [[nodiscard]] T* BodyAs() noexcept { return static_cast<T*>(Body()); }
          template <typename T> [[nodiscard]] const T* BodyAs() const noexcept { return static_cast<const T*>(Body()); }
     };

     [[nodiscard]] inline ObjectHeader* OiHeaderFromBody(void* body) noexcept
     {
          return reinterpret_cast<ObjectHeader*>(reinterpret_cast<std::uintptr_t>(body) - sizeof(ObjectHeader));
     }

     struct DirectoryBody
     {
          static ObjectHeader* const kTombstone;

          ObjectHeader** buckets{nullptr};
          std::size_t capacity{0};
          std::size_t count{0};
          std::size_t tombstones{0};

          [[nodiscard]] bool Insert(ObjectHeader* child) noexcept;

          bool Remove(std::string_view componentName) noexcept;

          [[nodiscard]] ObjectHeader* Find(std::string_view componentName) noexcept;
          [[nodiscard]] const ObjectHeader* Find(std::string_view componentName) const noexcept;

          using ChildCallback = void (*)(const ObjectHeader* child, void* ctx) noexcept;
          void Enumerate(ChildCallback callback, void* ctx) const noexcept;

          void Destroy() noexcept;

     private:
          [[nodiscard]] static std::size_t Hash(std::string_view name) noexcept;
          [[nodiscard]] bool Grow() noexcept;
     };

     void DirectoryObjectDestructor(void* body) noexcept;

     struct SymbolicLinkBody
     {
          std::string targetPath;

          explicit SymbolicLinkBody(std::string_view target) : targetPath(target) {}

          SymbolicLinkBody(const SymbolicLinkBody&) = delete;
          SymbolicLinkBody& operator=(const SymbolicLinkBody&) = delete;
          SymbolicLinkBody(SymbolicLinkBody&&) = default;
          SymbolicLinkBody& operator=(SymbolicLinkBody&&) = default;
     };

     void SymbolicLinkObjectDestructor(void* body) noexcept;

     using TypeOpenProc = bool (*)(ObjectHeader* object, AccessRights requested) noexcept;
     using TypeCloseProc = void (*)(ObjectHeader* object) noexcept;
     using TypeDeleteProc = void (*)(ObjectHeader* object) noexcept;

     struct TypeDescriptorBody
     {
          ObjectType typeId{ObjectType::Unknown};
          TypeOpenProc onOpen{nullptr};
          TypeCloseProc onClose{nullptr};
          TypeDeleteProc onDelete{nullptr};
     };

     struct ProcessorBody
     {
          std::uint32_t logicalIndex{};
          std::uint32_t apicId{};
          bool isOnline{true};
          bool isBsp{false};
     };
} // namespace object
