#include "object.h"

#include <cstdlib>
#include <cstring>
#include <tuple>

#include "../kinit.h"
#include "../process/process.h"
#include "../process/thread.h"
#include "utils/kdbg.h"

namespace object
{
     GlobalObjectDirectory g_directory{};

     Handle g_rootDirectoryHandle{kInvalidHandle};
     Handle g_globalRootHandle{kInvalidHandle};
     ObjectHeader* g_rootDirectoryHeader{nullptr};
     ObjectHeader* g_globalRootHeader{nullptr};

     namespace
     {
          bool _obInitialised = false;
     }

     static ObjectHeader* CreateSymbolicLink(kernel::ProcessControlBlock& pcb, ObjectHeader* parent,
                                             std::string_view name, std::string_view target) noexcept
     {
          ObjectHeader* hdr = OiAllocateObjectMemory(sizeof(SymbolicLinkBody));
          if (!hdr) return nullptr;

          OiInitialiseHeader(hdr, name, ObjectType::SymbolicLink, sizeof(SymbolicLinkBody),
                             SymbolicLinkObjectDestructor, AccessRights::All);

          new (hdr->Body()) SymbolicLinkBody{target};

          if (parent)
          {
               const Handle h = ObInsertObject(pcb, parent, hdr, AccessRights::All);
               (void)h;
          }

          return hdr;
     }

     static ObjectHeader* CreateDirectoryObject(kernel::ProcessControlBlock& pcb, ObjectHeader* parent,
                                                std::string_view name) noexcept
     {
          ObjectHeader* hdr = OiAllocateObjectMemory(sizeof(DirectoryBody));
          if (!hdr) return nullptr;

          OiInitialiseHeader(hdr, name, ObjectType::Directory, sizeof(DirectoryBody), DirectoryObjectDestructor,
                             AccessRights::All);

          new (hdr->Body()) DirectoryBody{};

          if (parent)
          {
               const Handle h = ObInsertObject(pcb, parent, hdr, AccessRights::All);
               (void)h;
          }

          return hdr;
     }

     void KeInitialiseOb() noexcept
     {
          _obInitialised = true;

          ObjectHeader* root = OiAllocateObjectMemory(sizeof(DirectoryBody));
          if (!root)
          {
               debugging::DbgWrite(u8"[KeInitialiseOb] OOM allocating root directory\r\n");
               return;
          }

          OiInitialiseHeader(root, "\\", ObjectType::Directory, sizeof(DirectoryBody), DirectoryObjectDestructor,
                             AccessRights::All);
          new (root->Body()) DirectoryBody{};

          std::ignore = g_directory.Insert(root);

          g_rootDirectoryHeader = root;
          g_rootDirectoryHandle = g_kernelProcess->GetHandleTable().AllocateSlot(root, AccessRights::All);

          OiReferenceObject(root);

          ObjectHeader* deviceDir = CreateDirectoryObject(*g_kernelProcess, root, "Device");
          ObjectHeader* driverDir = CreateDirectoryObject(*g_kernelProcess, root, "Driver");
          ObjectHeader* processorDir = CreateDirectoryObject(*g_kernelProcess, root, "Processor");
          ObjectHeader* objectTypesDir = CreateDirectoryObject(*g_kernelProcess, root, "ObjectTypes");

          (void)deviceDir;
          (void)driverDir;

          ObjectHeader* globalRoot = CreateDirectoryObject(*g_kernelProcess, root, "GlobalRoot");

          g_globalRootHeader = globalRoot;
          g_globalRootHandle = g_kernelProcess->GetHandleTable().AllocateSlot(globalRoot, AccessRights::All);
          OiReferenceObject(globalRoot);

          if (processorDir)
          {
               debugging::DbgWrite(u8"[KeInitialiseOb] Processor directory created\r\n");
               ObjectHeader* cpu0 = OiAllocateObjectMemory(sizeof(ProcessorBody));
               if (cpu0)
               {
                    OiInitialiseHeader(cpu0, "CPU0", ObjectType::Processor, sizeof(ProcessorBody), nullptr,
                                       AccessRights::All);
                    new (cpu0->Body()) ProcessorBody{
                        .logicalIndex = 0,
                        .apicId = 0,
                        .isOnline = true,
                        .isBsp = true,
                    };
                    std::ignore = ObInsertObject(*g_kernelProcess, processorDir, cpu0, AccessRights::All);
               }
               else
               {
                    debugging::DbgWrite(u8"[KeInitialiseOb] OOM allocating CPU0 processor object\r\n");
               }
          }
          else
          {
               debugging::DbgWrite(u8"[KeInitialiseOb] ERROR: Failed to create Processor directory!\r\n");
          }

          if (objectTypesDir)
          {
               debugging::DbgWrite(u8"[KeInitialiseOb] ObjectTypes directory created\r\n");
               struct // NOLINT
               {
                    ObjectType type;
                    const char* name;
               } constexpr types[] = {
                   {.type = ObjectType::Process, .name = "Process"},
                   {.type = ObjectType::Thread, .name = "Thread"},
                   {.type = ObjectType::File, .name = "File"},
                   {.type = ObjectType::Event, .name = "Event"},
                   {.type = ObjectType::Mutex, .name = "Mutex"},
                   {.type = ObjectType::Semaphore, .name = "Semaphore"},
                   {.type = ObjectType::Timer, .name = "Timer"},
                   {.type = ObjectType::Section, .name = "Section"},
                   {.type = ObjectType::Port, .name = "Port"},
                   {.type = ObjectType::SymbolicLink, .name = "SymbolicLink"},
                   {.type = ObjectType::Directory, .name = "Directory"},
                   {.type = ObjectType::Device, .name = "Device"},
                   {.type = ObjectType::Driver, .name = "Driver"},
                   {.type = ObjectType::TypeDescriptor, .name = "TypeDescriptor"},
                   {.type = ObjectType::Processor, .name = "Processor"},
               };

               for (const auto& t : types)
               {
                    ObjectHeader* td = OiAllocateObjectMemory(sizeof(TypeDescriptorBody));
                    if (!td) continue;
                    OiInitialiseHeader(td, t.name, ObjectType::TypeDescriptor, sizeof(TypeDescriptorBody), nullptr,
                                       AccessRights::Read);
                    new (td->Body()) TypeDescriptorBody{.typeId = t.type};
                    std::ignore = ObInsertObject(*g_kernelProcess, objectTypesDir, td, AccessRights::Read);
               }
          }
          else
          {
               debugging::DbgWrite(u8"[KeInitialiseOb] ERROR: Failed to create ObjectTypes directory!\r\n");
          }

          debugging::DbgWrite(u8"[KeInitialiseOb] Namespace tree initialised\r\n");
          debugging::DbgWrite(u8"  \\              (root directory)\r\n");
          debugging::DbgWrite(u8"  \\Device        (device namespace)\r\n");
          debugging::DbgWrite(u8"  \\Driver        (driver namespace)\r\n");
          debugging::DbgWrite(u8"  \\Processor     (processor namespace)\r\n");
          debugging::DbgWrite(u8"  \\Processor\\CPU0 (BSP)\r\n");
          debugging::DbgWrite(u8"  \\ObjectTypes   (type descriptor namespace)\r\n");
          debugging::DbgWrite(u8"  \\GlobalRoot    (global symbolic link directory)\r\n");
     }

     std::size_t GlobalObjectDirectory::Hash(std::string_view name) noexcept
     {
          constexpr std::uint64_t kBasis = 0xcbf2'9ce4'8422'2325ULL;
          constexpr std::uint64_t kPrime = 0x0000'0100'0000'01b3ULL;

          std::uint64_t h = kBasis;
          for (const char c : name)
          {
               h ^= static_cast<std::uint8_t>(c);
               h *= kPrime;
          }

          return static_cast<std::size_t>(h & (kGlobalDirectoryBuckets - 1));
     }

     bool GlobalObjectDirectory::Insert(ObjectHeader* header) noexcept
     {
          const std::string_view name = header->accessName.View();
          if (name.empty()) return true;

          if (Find(name) != nullptr)
          {
               debugging::DbgWrite(u8"[GlobalObjectDirectory] Name collision for \"{}\"\r\n", name);
               return false;
          }

          const std::size_t bucket = Hash(name);
          header->nextInBucket = _buckets[bucket];
          _buckets[bucket] = header;
          return true;
     }

     void GlobalObjectDirectory::Remove(ObjectHeader* header) noexcept
     {
          const std::string_view name = header->accessName.View();
          if (name.empty()) return;

          const std::size_t bucket = Hash(name);
          ObjectHeader** prev = &_buckets[bucket];

          while (*prev)
          {
               if (*prev == header)
               {
                    *prev = header->nextInBucket;
                    header->nextInBucket = nullptr;
                    return;
               }
               prev = &(*prev)->nextInBucket;
          }

          debugging::DbgWrite(u8"[GlobalObjectDirectory] Remove: header \"{}\" not found in bucket\r\n", name);
     }

     ObjectHeader* GlobalObjectDirectory::Find(std::string_view name) noexcept
     {
          if (name.empty()) return nullptr;

          const std::size_t bucket = Hash(name);
          for (ObjectHeader* cur = _buckets[bucket]; cur; cur = cur->nextInBucket)
          {
               if (cur->accessName.View() == name) return cur;
          }

          return nullptr;
     }

     const ObjectHeader* GlobalObjectDirectory::Find(std::string_view name) const noexcept
     {
          return const_cast<GlobalObjectDirectory*>(this)->Find(name); // NOLINT
     }

     ObjectHeader* OiAllocateObjectMemory(std::size_t bodySize) noexcept
     {
          const std::size_t total = sizeof(ObjectHeader) + bodySize;
          void* raw = operator new(total);
          if (!raw) return nullptr;
          std::memset(raw, 0, total);
          return static_cast<ObjectHeader*>(raw);
     }

     void OiFreeObjectMemory(ObjectHeader* header) noexcept { operator delete(header); }

     void OiInitialiseHeader(ObjectHeader* header, std::string_view name, ObjectType type, std::uint32_t bodySize,
                             ObjectDestructor destructor, AccessRights access) noexcept
     {
          header->accessName = ObjectName{name};
          header->type = type;
          header->bodySize = bodySize;
          header->destructor = destructor;
          header->grantedAccess = access;
          header->referenceCount.store(1, std::memory_order::relaxed);
          header->nextInBucket = nullptr;
     }

     std::int32_t OiReferenceObject(ObjectHeader* header) noexcept
     {
          return header->referenceCount.fetch_add(1, std::memory_order::relaxed) + 1;
     }

     ObjectHeader* OiResolvePath(ObjectHeader* startDir, std::string_view path, OpenFlags flags,
                                 ObjectStatus* outStatus) noexcept
     {
          if (!startDir || startDir->type != ObjectType::Directory)
          {
               if (outStatus) *outStatus = ObjectStatus::NotADirectory;
               return nullptr;
          }

          ObjectHeader* currentDir = startDir;
          OiReferenceObject(currentDir);

          std::string_view remaining = path;

          while (!remaining.empty() && remaining[0] == '\\') remaining.remove_prefix(1);

          std::uint32_t symlinkDepth = 0;

          constexpr std::size_t kPathBufSize = 256;
          std::array<char, kPathBufSize> pathBuf{};
          bool usingBuf = false;
          std::string_view bufView{};

          const auto setStatus = [&](ObjectStatus s)
          {
               if (outStatus) *outStatus = s;
          };

          while (true)
          {
               if (remaining.empty())
               {
                    if (outStatus) *outStatus = ObjectStatus::Success;
                    return currentDir;
               }

               const std::size_t sep = remaining.find('\\');
               const std::string_view component =
                   (sep == std::string_view::npos) ? remaining : remaining.substr(0, sep);
               const bool isFinalComponent = (sep == std::string_view::npos);

               if (sep == std::string_view::npos) remaining = {};
               else
               {
                    remaining.remove_prefix(sep + 1);

                    while (!remaining.empty() && remaining[0] == '\\') remaining.remove_prefix(1);
               }

               if (component.empty()) continue;

               if (currentDir->type != ObjectType::Directory)
               {
                    OiDereferenceObject(currentDir);
                    setStatus(ObjectStatus::NotADirectory);
                    return nullptr;
               }

               auto* dirBody = currentDir->BodyAs<DirectoryBody>();
               ObjectHeader* child = dirBody->Find(component);

               if (!child)
               {
                    OiDereferenceObject(currentDir);
                    setStatus(ObjectStatus::NotFound);
                    return nullptr;
               }

               OiReferenceObject(child);
               OiDereferenceObject(currentDir);
               currentDir = child;

               const bool shouldFollow = currentDir->type == ObjectType::SymbolicLink &&
                                         !(isFinalComponent && HasFlag(flags, OpenFlags::NoFollow));

               if (shouldFollow)
               {
                    if (++symlinkDepth > kMaxSymlinkDepth)
                    {
                         OiDereferenceObject(currentDir);
                         setStatus(ObjectStatus::SymbolicLinkLoop);
                         return nullptr;
                    }

                    const auto* slBody = currentDir->BodyAs<SymbolicLinkBody>();
                    std::string_view target = slBody->targetPath;

                    std::size_t pos = 0;
                    auto append = [&](std::string_view sv)
                    {
                         for (char c : sv)
                              if (pos + 1 < kPathBufSize) pathBuf[pos++] = c;
                    };

                    append(target);
                    if (!remaining.empty() && (pos == 0 || pathBuf[pos - 1] != '\\'))
                         if (pos + 1 < kPathBufSize) pathBuf[pos++] = '\\';
                    append(remaining);
                    pathBuf[pos] = '\0';
                    bufView = std::string_view{pathBuf.data(), pos};
                    usingBuf = true;

                    OiDereferenceObject(currentDir);

                    if (!bufView.empty() && bufView[0] == '\\')
                    {
                         bufView.remove_prefix(1);
                         while (!bufView.empty() && bufView[0] == '\\') bufView.remove_prefix(1);
                         currentDir = g_rootDirectoryHeader;
                    }
                    else
                    {
                         currentDir = startDir;
                    }

                    OiReferenceObject(currentDir);
                    remaining = bufView;
                    continue;
               }
          }
     }

     std::int32_t OiDereferenceObject(ObjectHeader* header) noexcept
     {
          const std::int32_t previous = header->referenceCount.fetch_sub(1, std::memory_order::acq_rel);

          if (previous == 1)
          {
               g_directory.Remove(header);

               if (header->destructor) header->destructor(header->Body());

               OiFreeObjectMemory(header);
               return -1;
          }

          return previous - 1;
     }

     HandleTable::HandleTable() noexcept : _freeTop(kMaxHandlesPerProcess), _freeCount(kMaxHandlesPerProcess)
     {
          for (std::size_t i = 0; i < kMaxHandlesPerProcess; ++i)
               _freeStack[i] = static_cast<std::uint16_t>(kMaxHandlesPerProcess - 1 - i);
     }

     Handle HandleTable::AllocateSlot(ObjectHeader* header, AccessRights access) noexcept
     {
          if (_freeCount == 0) return kInvalidHandle;

          const std::size_t slotIndex = _freeStack[--_freeTop];
          --_freeCount;
          ++_activeCount;

          HandleTableEntry& entry = _slots[slotIndex];
          entry.header = header;
          entry.access = access;
          ++entry.generation;

          return OiIndexToHandle(slotIndex);
     }

     ObjectStatus HandleTable::FreeSlot(Handle handle) noexcept
     {
          if (!OiIsValidHandle(handle)) return ObjectStatus::InvalidHandle;

          const std::size_t idx = OiHandleToIndex(handle);
          if (idx >= kMaxHandlesPerProcess) return ObjectStatus::InvalidHandle;

          HandleTableEntry& entry = _slots[idx];
          if (!entry.header) return ObjectStatus::InvalidHandle;

          ObjectHeader* header = entry.header;

          entry.header = nullptr;
          entry.access = AccessRights::None;

          _freeStack[_freeTop++] = static_cast<std::uint16_t>(idx);
          ++_freeCount;
          --_activeCount;

          OiDereferenceObject(header);

          return ObjectStatus::Success;
     }

     HandleTableEntry* HandleTable::LookupEntry(Handle handle) noexcept
     {
          if (!OiIsValidHandle(handle)) return nullptr;

          const std::size_t idx = OiHandleToIndex(handle);
          if (idx >= kMaxHandlesPerProcess) return nullptr;

          HandleTableEntry& entry = _slots[idx];
          return entry.header ? &entry : nullptr;
     }

     const HandleTableEntry* HandleTable::LookupEntry(Handle handle) const noexcept
     {
          return const_cast<HandleTable*>(this)->LookupEntry(handle); // NOLINT
     }

     ObjectHeader* HandleTable::LookupHeader(Handle handle) noexcept
     {
          HandleTableEntry* e = LookupEntry(handle);
          return e ? e->header : nullptr;
     }

     Handle ObCreateObject(kernel::ProcessControlBlock& pcb, const ObjectAttributes& attributes) noexcept
     {
          if (!attributes.name.empty() && g_directory.Find(attributes.name) != nullptr)
          {
               debugging::DbgWrite(u8"[ObCreateObject] Name collision for \"{}\"\r\n", attributes.name);
               return kInvalidHandle;
          }

          ObjectHeader* header = OiAllocateObjectMemory(attributes.bodySize);
          if (!header)
          {
               debugging::DbgWrite(u8"[ObCreateObject] OOM for object \"{}\"\r\n", attributes.name);
               return kInvalidHandle;
          }

          OiInitialiseHeader(header, attributes.name, attributes.type, attributes.bodySize, attributes.destructor,
                             attributes.desiredAccess);

          if (!g_directory.Insert(header))
          {
               debugging::DbgWrite(u8"[ObCreateObject] Failed to insert object \"{}\" into global directory\r\n",
                                   attributes.name);
               OiFreeObjectMemory(header);
               return kInvalidHandle;
          }

          const Handle handle = pcb.GetHandleTable().AllocateSlot(header, attributes.desiredAccess);
          if (handle == kInvalidHandle)
          {
               debugging::DbgWrite(u8"[ObCreateObject] Handle table full for \"{}\"\r\n", attributes.name);

               g_directory.Remove(header);
               OiFreeObjectMemory(header);
               return kInvalidHandle;
          }

          return handle;
     }
     Handle ObCreateObjectIn(kernel::ProcessControlBlock& pcb, const ObjectAttributes& attributes,
                             std::string_view parentPath) noexcept
     {
          if (!g_rootDirectoryHeader)
          {
               debugging::DbgWrite(u8"[ObCreateObjectIn] Namespace not initialised\r\n");
               return kInvalidHandle;
          }

          ObjectStatus status = ObjectStatus::Success;
          ObjectHeader* parent = OiResolvePath(g_rootDirectoryHeader, parentPath, OpenFlags::None, &status);

          if (!parent)
          {
               debugging::DbgWrite(u8"[ObCreateObjectIn] Failed to resolve parent \"{}\" (status {})\r\n", parentPath,
                                   static_cast<std::int32_t>(status));
               return kInvalidHandle;
          }

          if (parent->type != ObjectType::Directory)
          {
               debugging::DbgWrite(u8"[ObCreateObjectIn] Parent is not a directory\r\n");
               OiDereferenceObject(parent);
               return kInvalidHandle;
          }

          auto* dirBody = parent->BodyAs<DirectoryBody>();
          if (!attributes.name.empty() && dirBody->Find(attributes.name) != nullptr)
          {
               debugging::DbgWrite(u8"[ObCreateObjectIn] Name collision for \"{}\"\r\n", attributes.name);
               OiDereferenceObject(parent);
               return kInvalidHandle;
          }

          ObjectHeader* header = OiAllocateObjectMemory(attributes.bodySize);
          if (!header)
          {
               debugging::DbgWrite(u8"[ObCreateObjectIn] OOM for object \"{}\"\r\n", attributes.name);
               OiDereferenceObject(parent);
               return kInvalidHandle;
          }

          OiInitialiseHeader(header, attributes.name, attributes.type, attributes.bodySize, attributes.destructor,
                             attributes.desiredAccess);

          const Handle handle = ObInsertObject(pcb, parent, header, attributes.desiredAccess);
          if (handle == kInvalidHandle)
          {
               debugging::DbgWrite(u8"[ObCreateObjectIn] Insert failed for \"{}\"\r\n", attributes.name);
               OiFreeObjectMemory(header);
               OiDereferenceObject(parent);
               return kInvalidHandle;
          }

          OiDereferenceObject(parent);
          return handle;
     }

     void KeRegisterProcessorObject(kernel::ProcessControlBlock& pcb, std::uint32_t logicalIndex,
                                    std::uint32_t apicId) noexcept
     {
          if (!g_rootDirectoryHeader) return;

          auto* rootBody = g_rootDirectoryHeader->BodyAs<DirectoryBody>();
          ObjectHeader* procDir = rootBody->Find("Processor");
          if (!procDir || procDir->type != ObjectType::Directory) return;

          std::array<char, kObjectNameCapacity> nameBuf{};
          nameBuf[0] = 'C';
          nameBuf[1] = 'P';
          nameBuf[2] = 'U';

          std::array<char, 12> digitBuf{};
          std::size_t dLen = 0;
          std::uint32_t n = logicalIndex;
          do
          {
               digitBuf[dLen++] = static_cast<char>('0' + (n % 10));
               n /= 10;
          } while (n);
          for (std::size_t i = 0; i < dLen; ++i) nameBuf[3 + i] = digitBuf[dLen - 1 - i];
          const std::string_view cpuName{nameBuf.data(), 3 + dLen};

          ObjectHeader* cpu = OiAllocateObjectMemory(sizeof(ProcessorBody));
          if (!cpu) return;

          OiInitialiseHeader(cpu, cpuName, ObjectType::Processor, sizeof(ProcessorBody), nullptr, AccessRights::All);
          new (cpu->Body()) ProcessorBody{
              .logicalIndex = logicalIndex,
              .apicId = apicId,
              .isOnline = true,
              .isBsp = false,
          };

          std::ignore = ObInsertObject(pcb, procDir, cpu, AccessRights::All);
     }

     Handle ObOpenObjectByName(kernel::ProcessControlBlock& pcb, std::string_view name,
                               AccessRights desiredAccess) noexcept
     {
          ObjectHeader* header = g_directory.Find(name);
          if (header == nullptr) { return kInvalidHandle; }

          if (!HasAccess(header->grantedAccess, desiredAccess))
          {
               debugging::DbgWrite(u8"[ObOpenObjectByName] Access denied opening \"{}\"\r\n", name);
               return kInvalidHandle;
          }

          OiReferenceObject(header);

          const Handle handle = pcb.GetHandleTable().AllocateSlot(header, desiredAccess);
          if (handle == kInvalidHandle)
          {
               debugging::DbgWrite(u8"[ObOpenObjectByName] Handle table full opening \"{}\"\r\n", name);
               OiDereferenceObject(header);
               return kInvalidHandle;
          }

          return handle;
     }

     bool DirectoryBody::Insert(ObjectHeader* child) noexcept
     {
          if ((count + tombstones + 1) * 4 > capacity * 3)
          {
               if (!Grow()) return false;
          }

          const std::size_t mask = capacity - 1;
          std::size_t idx = Hash(child->accessName.View()) & mask;

          std::size_t firstTomb = capacity;
          for (std::size_t probe = 0; probe <= capacity; ++probe, idx = (idx + 1) & mask)
          {
               ObjectHeader* slot = buckets[idx];

               if (slot == nullptr)
               {
                    const std::size_t insertIdx = (firstTomb < capacity) ? firstTomb : idx;
                    if (firstTomb < capacity) --tombstones;
                    buckets[insertIdx] = child;
                    ++count;
                    OiReferenceObject(child);
                    return true;
               }

               if (slot == kTombstone)
               {
                    if (firstTomb == capacity) firstTomb = idx;
                    continue;
               }

               if (slot->accessName.View() == child->accessName.View()) return false; // name collision
          }

          return false;
     }

     bool DirectoryBody::Remove(std::string_view componentName) noexcept
     {
          if (capacity == 0) return false;

          const std::size_t mask = capacity - 1;
          std::size_t idx = Hash(componentName) & mask;

          for (std::size_t probe = 0; probe <= capacity; ++probe, idx = (idx + 1) & mask)
          {
               ObjectHeader* slot = buckets[idx];
               if (slot == nullptr) return false;
               if (slot == kTombstone) continue;

               if (slot->accessName.View() == componentName)
               {
                    OiDereferenceObject(slot);
                    buckets[idx] = kTombstone;
                    --count;
                    ++tombstones;
                    return true;
               }
          }

          return false;
     }

     ObjectHeader* DirectoryBody::Find(std::string_view componentName) noexcept // NOLINT
     {
          if (capacity == 0) return nullptr;

          const std::size_t mask = capacity - 1;
          std::size_t idx = Hash(componentName) & mask;

          for (std::size_t probe = 0; probe <= capacity; ++probe, idx = (idx + 1) & mask)
          {
               ObjectHeader* slot = buckets[idx];
               if (slot == nullptr) return nullptr;
               if (slot == kTombstone) continue;
               if (slot->accessName.View() == componentName) return slot;
          }

          return nullptr;
     }

     const ObjectHeader* DirectoryBody::Find(std::string_view componentName) const noexcept
     {
          return const_cast<DirectoryBody*>(this)->Find(componentName); // NOLINT
     }

     void DirectoryBody::Enumerate(ChildCallback callback, void* ctx) const noexcept
     {
          for (std::size_t i = 0; i < capacity; ++i)
          {
               const ObjectHeader* slot = buckets[i];
               if (slot && slot != kTombstone) callback(slot, ctx);
          }
     }

     void DirectoryBody::Destroy() noexcept
     {
          for (std::size_t i = 0; i < capacity; ++i)
          {
               ObjectHeader* slot = buckets[i];
               if (slot && slot != kTombstone)
               {
                    slot->parentDirectory = nullptr;
                    OiDereferenceObject(slot);
               }
          }

          operator delete(buckets);
          buckets = nullptr;
          capacity = 0;
          count = 0;
          tombstones = 0;
     }
     ObjectHeader* const DirectoryBody::kTombstone = reinterpret_cast<ObjectHeader*>(static_cast<std::uintptr_t>(1));

     void GlobalObjectDirectory::Enumerate(EnumerateCallback callback, void* context) const noexcept
     {
          for (std::size_t i = 0; i < kGlobalDirectoryBuckets; ++i)
               for (const ObjectHeader* cur = _buckets[i]; cur; cur = cur->nextInBucket) callback(i, cur, context);
     }

     std::size_t DirectoryBody::Hash(std::string_view name) noexcept
     {
          constexpr std::uint64_t kBasis = 0xcbf2'9ce4'8422'2325ULL;
          constexpr std::uint64_t kPrime = 0x0000'0100'0000'01b3ULL;
          std::uint64_t h = kBasis;
          for (const char c : name)
          {
               h ^= static_cast<std::uint8_t>(c);
               h *= kPrime;
          }
          return static_cast<std::size_t>(h);
     }

     inline constexpr std::size_t kDirectoryInitialBuckets = 16;

     bool DirectoryBody::Grow() noexcept
     {
          const std::size_t newCap = capacity == 0 ? kGlobalDirectoryBuckets : capacity * 2;
          auto* newBuckets = static_cast<ObjectHeader**>(operator new(newCap * sizeof(ObjectHeader*)));
          if (!newBuckets) return false;

          for (std::size_t i = 0; i < newCap; ++i) newBuckets[i] = nullptr;

          for (std::size_t i = 0; i < capacity; ++i)
          {
               ObjectHeader* h = buckets[i];
               if (!h || h == kTombstone) continue;

               std::size_t idx = Hash(h->accessName.View()) & (newCap - 1);
               while (newBuckets[idx] != nullptr) idx = (idx + 1) & (newCap - 1);
               newBuckets[idx] = h;
          }

          operator delete(buckets);
          buckets = newBuckets;
          capacity = newCap;
          tombstones = 0;
          return true;
     }

     ObjectStatus ObCloseHandle(kernel::ProcessControlBlock& pcb, Handle handle) noexcept
     {
          return pcb.GetHandleTable().FreeSlot(handle);
     }

     Handle ObDuplicateHandle(kernel::ProcessControlBlock& srcPcb, kernel::ProcessControlBlock& dstPcb,
                              Handle sourceHandle, AccessRights desiredAccess) noexcept
     {
          HandleTableEntry* srcEntry = srcPcb.GetHandleTable().LookupEntry(sourceHandle);
          if (!srcEntry) return kInvalidHandle;

          const AccessRights effectiveAccess = srcEntry->access & desiredAccess;

          OiReferenceObject(srcEntry->header);

          const Handle dup = dstPcb.GetHandleTable().AllocateSlot(srcEntry->header, effectiveAccess);
          if (dup == kInvalidHandle)
          {
               debugging::DbgWrite(u8"[ObDuplicateHandle] Destination handle table full\r\n");
               OiDereferenceObject(srcEntry->header);
               return kInvalidHandle;
          }

          return dup;
     }

     ObjectHeader* ObQueryObject(kernel::ProcessControlBlock& pcb, Handle handle) noexcept
     {
          return pcb.GetHandleTable().LookupHeader(handle);
     }

     template <typename T>
     T* ObGetBody(kernel::ProcessControlBlock& pcb, Handle handle, ObjectType expectedType) noexcept
     {
          ObjectHeader* header = pcb.GetHandleTable().LookupHeader(handle);
          if (!header || header->type != expectedType) return nullptr;
          return header->BodyAs<T>();
     }

     Handle ObInsertObject(kernel::ProcessControlBlock& pcb, ObjectHeader* parentDir, ObjectHeader* header,
                           AccessRights desiredAccess) noexcept
     {
          if (!parentDir || parentDir->type != ObjectType::Directory)
          {
               debugging::DbgWrite(u8"[ObInsertObject] Parent is not a directory\r\n");
               return kInvalidHandle;
          }

          auto* dirBody = parentDir->BodyAs<DirectoryBody>();
          if (!dirBody->Insert(header))
          {
               debugging::DbgWrite(u8"[ObInsertObject] Insert failed (collision or OOM) for \"{}\"\r\n",
                                   header->accessName.View());
               return kInvalidHandle;
          }

          header->parentDirectory = parentDir;

          const Handle handle = pcb.GetHandleTable().AllocateSlot(header, desiredAccess);
          if (handle == kInvalidHandle)
          {
               debugging::DbgWrite(u8"[ObInsertObject] Handle table full\r\n");
               dirBody->Remove(header->accessName.View());
               header->parentDirectory = nullptr;
               return kInvalidHandle;
          }

          return handle;
     }

     void DirectoryObjectDestructor(void* body) noexcept { static_cast<DirectoryBody*>(body)->Destroy(); }

     void SymbolicLinkObjectDestructor(void* body) noexcept
     {
          static_cast<SymbolicLinkBody*>(body)->~SymbolicLinkBody();
     }

     Handle ObOpenObjectByAbsolutePath(kernel::ProcessControlBlock& pcb, std::string_view path,
                                       AccessRights desiredAccess, OpenFlags flags) noexcept
     {
          if (!g_rootDirectoryHeader)
          {
               debugging::DbgWrite(u8"[ObOpenObjectByAbsolutePath] Namespace not initialised\r\n");
               return kInvalidHandle;
          }

          while (!path.empty() && path[0] == '\\') path.remove_prefix(1);

          ObjectStatus status = ObjectStatus::Success;
          ObjectHeader* resolved = OiResolvePath(g_rootDirectoryHeader, path, flags, &status);
          if (!resolved)
          {
               debugging::DbgWrite(u8"[ObOpenObjectByAbsolutePath] Not found: status {}\r\n",
                                   static_cast<std::int32_t>(status));
               return kInvalidHandle;
          }

          if (!HasAccess(resolved->grantedAccess, desiredAccess))
          {
               OiDereferenceObject(resolved);
               return kInvalidHandle;
          }

          const Handle handle = pcb.GetHandleTable().AllocateSlot(resolved, desiredAccess);
          if (handle == kInvalidHandle)
          {
               debugging::DbgWrite(u8"[ObOpenObjectByAbsolutePath] Handle table full\r\n");
               OiDereferenceObject(resolved);
               return kInvalidHandle;
          }

          return handle;
     }

     Handle ObOpenObjectByPath(kernel::ProcessControlBlock& pcb, std::string_view path, AccessRights desiredAccess,
                               OpenFlags flags) noexcept
     {
          if (path.empty()) return kInvalidHandle;

          if (path[0] == '\\') return ObOpenObjectByAbsolutePath(pcb, path, desiredAccess, flags);

          if (!g_globalRootHeader)
          {
               debugging::DbgWrite(u8"[ObOpenObjectByPath] \\GlobalRoot not available\r\n");
               return kInvalidHandle;
          }

          constexpr std::size_t kBuf = 256;
          std::array<char, kBuf> buf{};
          std::size_t pos = 0;

          auto append = [&](std::string_view sv)
          {
               for (char c : sv)
                    if (pos + 1 < kBuf) buf[pos++] = c;
          };

          append("GlobalRoot");
          append("\\");
          append(path);
          buf[pos] = '\0';

          const std::string_view combined{buf.data(), pos};

          ObjectStatus status = ObjectStatus::Success;
          ObjectHeader* resolved = OiResolvePath(g_rootDirectoryHeader, combined, flags, &status);
          if (!resolved)
          {
               debugging::DbgWrite(u8"[ObOpenObjectByPath] Resolution failed: status {}\r\n",
                                   static_cast<std::int32_t>(status));
               return kInvalidHandle;
          }

          if (!HasAccess(resolved->grantedAccess, desiredAccess))
          {
               OiDereferenceObject(resolved);
               return kInvalidHandle;
          }

          const Handle handle = pcb.GetHandleTable().AllocateSlot(resolved, desiredAccess);
          if (handle == kInvalidHandle)
          {
               debugging::DbgWrite(u8"[ObOpenObjectByPath] Handle table full\r\n");
               OiDereferenceObject(resolved);
               return kInvalidHandle;
          }

          return handle;
     }

     template <>
     void* ObGetBody<void>(kernel::ProcessControlBlock& pcb, Handle handle, ObjectType expectedType) noexcept
     {
          ObjectHeader* header = pcb.GetHandleTable().LookupHeader(handle);
          if (!header || header->type != expectedType) return nullptr;
          return header->BodyAs<void>();
     }

     template <>
     process::Thread* ObGetBody<process::Thread>(kernel::ProcessControlBlock& pcb, Handle handle,
                                                 ObjectType expectedType) noexcept
     {
          ObjectHeader* header = pcb.GetHandleTable().LookupHeader(handle);
          if (!header || header->type != expectedType) return nullptr;
          return header->BodyAs<process::Thread>();
     }

} // namespace object
