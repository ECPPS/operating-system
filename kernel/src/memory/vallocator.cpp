#include "vallocator.h"
#include <utils/identify.h>
#include <utils/memory.h>
#include <algorithm>
#include <atomic>
#include "../process/taskScheduler.h"
#include "utils/kdbg.h"
#include "utils/operations.h"

static constexpr std::size_t AlignUp(std::size_t value, std::size_t align)
{
     return (value + align - 1) & ~(align - 1);
}

namespace memory
{
     constexpr std::size_t PAGE_SIZE = 4096;

     struct FreeNode
     {
          FreeNode* next;
     };

     void VADNodeAllocator::AcquireLock() noexcept
     {
          bool expected = false;
          while (!_lock.compare_exchange_weak(expected, true, std::memory_order::acquire, std::memory_order::relaxed))
          {
               expected = false;
               operations::Yield();
          }
     }

     void VADNodeAllocator::ReleaseLock() noexcept { _lock.store(false, std::memory_order::release); }

     bool VADNodeAllocator::AllocateNodePage()
     {
          std::uintptr_t physPage = physicalAllocator.AllocatePage(PFNUse::KernelHeap);
          if (physPage == ~0uz)
          {
               debugging::DbgWrite(u8"[VADNodeAllocator::AllocateNodePage] physicalAllocator.AllocatePage == ~0\r\n");
               return false;
          }

          auto* page = reinterpret_cast<struct FreeNode*>(physPage + physicalAllocator.physToVirtOffset);
          auto* nodeArray = reinterpret_cast<VADNode*>(page);

          const std::size_t nodesPerPage = PAGE_SIZE / sizeof(VADNode);
          for (std::size_t i = 0; i < nodesPerPage; i++)
          {
               auto* freeNode = reinterpret_cast<struct FreeNode*>(&nodeArray[i]);
               freeNode->next = reinterpret_cast<struct FreeNode*>(_freeList);
               _freeList = freeNode;
          }

          return true;
     }

     VADNode* VADNodeAllocator::AllocateNode(const VADEntry& entry)
     {
          AcquireLock();

          if (_freeList == nullptr)
          {
               if (!AllocateNodePage())
               {
                    ReleaseLock();
                    return nullptr;
               }
          }

          auto* freeNode = reinterpret_cast<struct FreeNode*>(_freeList);
          _freeList = freeNode->next;

          auto* node = reinterpret_cast<VADNode*>(freeNode);
          new (node) VADNode(entry);

          _nodesAllocated++;
          ReleaseLock();
          return node;
     }

     void VADNodeAllocator::FreeNode(VADNode* node)
     {
          if (node == nullptr)
          {
               debugging::DbgWrite(u8"[FreeNode] node == nullptr\r\n");
               return;
          }

          node->~VADNode();

          AcquireLock();
          auto* freeNode = reinterpret_cast<struct FreeNode*>(node);
          freeNode->next = reinterpret_cast<struct FreeNode*>(_freeList);
          _freeList = freeNode;
          _nodesFreed++;
          ReleaseLock();
     }

     void VirtualMemoryAllocator::AcquireLock() noexcept
     {
          bool expected = false;
          while (!_lock.compare_exchange_weak(expected, true, std::memory_order::acquire, std::memory_order::relaxed))
          {
               expected = false;
               operations::Yield();
          }
     }

     void VirtualMemoryAllocator::ReleaseLock() noexcept { _lock.store(false, std::memory_order::release); }

     void VirtualMemoryAllocator::UpdateMaxEnd(VADNode* node)
     {
          if (node == nullptr)
          {
               debugging::DbgWrite(u8"[UpdateMaxEnd] node == nullptr\r\n");
               return;
          }
          node->maxEnd = node->EndAddress();
          if (node->left && node->left->maxEnd > node->maxEnd) node->maxEnd = node->left->maxEnd;
          if (node->right && node->right->maxEnd > node->maxEnd) node->maxEnd = node->right->maxEnd;
     }

     void VirtualMemoryAllocator::UpdateMaxEndUpwards(VADNode* node)
     {
          while (node != nullptr)
          {
               UpdateMaxEnd(node);
               node = node->parent;
          }
     }

     void VirtualMemoryAllocator::RotateLeft(VADNode* x)
     {
          VADNode* y = x->right;
          x->right = y->left;

          if (y->left) y->left->parent = x;

          y->parent = x->parent;

          if (!x->parent) _root = y;
          else if (x == x->parent->left)
               x->parent->left = y;
          else
               x->parent->right = y;

          y->left = x;
          x->parent = y;

          UpdateMaxEnd(x);
          UpdateMaxEnd(y);
     }

     void VirtualMemoryAllocator::RotateRight(VADNode* y)
     {
          VADNode* x = y->left;
          y->left = x->right;

          if (x->right) x->right->parent = y;

          x->parent = y->parent;

          if (!y->parent) _root = x;
          else if (y == y->parent->left)
               y->parent->left = x;
          else
               y->parent->right = x;

          x->right = y;
          y->parent = x;

          UpdateMaxEnd(y);
          UpdateMaxEnd(x);
     }

     void VirtualMemoryAllocator::FixInsert(VADNode* z)
     {
          while (z->parent && z->parent->colour == RBColour::Red)
          {
               if (z->parent == z->parent->parent->left)
               {
                    VADNode* y = z->parent->parent->right;
                    if (y && y->colour == RBColour::Red)
                    {
                         z->parent->colour = RBColour::Black;
                         y->colour = RBColour::Black;
                         z->parent->parent->colour = RBColour::Red;
                         z = z->parent->parent;
                    }
                    else
                    {
                         if (z == z->parent->right)
                         {
                              z = z->parent;
                              RotateLeft(z);
                         }
                         z->parent->colour = RBColour::Black;
                         z->parent->parent->colour = RBColour::Red;
                         RotateRight(z->parent->parent);
                    }
               }
               else
               {
                    VADNode* y = z->parent->parent->left;
                    if (y && y->colour == RBColour::Red)
                    {
                         z->parent->colour = RBColour::Black;
                         y->colour = RBColour::Black;
                         z->parent->parent->colour = RBColour::Red;
                         z = z->parent->parent;
                    }
                    else
                    {
                         if (z == z->parent->left)
                         {
                              z = z->parent;
                              RotateRight(z);
                         }
                         z->parent->colour = RBColour::Black;
                         z->parent->parent->colour = RBColour::Red;
                         RotateLeft(z->parent->parent);
                    }
               }
          }
          _root->colour = RBColour::Black;
     }

     bool VirtualMemoryAllocator::Insert(const VADEntry& entry)
     {
          if (FindOverlap(entry.baseAddress, entry.size) != nullptr)
          {
               debugging::DbgWrite(u8"[VirtualMemoryAllocator::Insert] FindOverlap({}, {}) != nullptr\r\n",
                                   reinterpret_cast<void*>(entry.baseAddress), entry.size);
               return false;
          }

          VADNode* z = _nodeAllocator.AllocateNode(entry);
          if (z == nullptr)
          {
               debugging::DbgWrite(
                   u8"[VirtualMemoryAllocator::Insert] _nodeAllocator.AllocateNode(entry) == nullptr\r\n");
               return false;
          }

          VADNode* y = nullptr;
          VADNode* x = _root;

          while (x)
          {
               y = x;
               if (z->entry.baseAddress < x->entry.baseAddress) x = x->left;
               else
                    x = x->right;
          }

          z->parent = y;

          if (y == nullptr) _root = z;
          else if (z->entry.baseAddress < y->entry.baseAddress)
               y->left = z;
          else
               y->right = z;

          UpdateMaxEndUpwards(z);
          FixInsert(z);
          return true;
     }

     VADNode* VirtualMemoryAllocator::Search(std::uintptr_t baseAddress)
     {
          VADNode* current = _root;

          while (current)
          {
               if (baseAddress == current->entry.baseAddress) return current;
               if (baseAddress < current->entry.baseAddress) current = current->left;
               else
                    current = current->right;
          }

          debugging::DbgWrite(u8"[Search] {} not found\r\n", reinterpret_cast<void*>(baseAddress));
          return nullptr;
     }

     VADNode* VirtualMemoryAllocator::FindOverlap(std::uintptr_t baseAddress, std::size_t size)
     {
          const std::uintptr_t endAddress = baseAddress + size;
          VADNode* current = _root;

          while (current)
          {
               if (baseAddress < current->EndAddress() && endAddress > current->entry.baseAddress) return current;

               if (current->left && current->left->maxEnd > baseAddress) current = current->left;
               else
                    current = current->right;
          }

          return nullptr;
     }

     VADNode* VirtualMemoryAllocator::FindContaining(std::uintptr_t address)
     {
          VADNode* current = _root;

          while (current)
          {
               if (address >= current->entry.baseAddress && address < current->EndAddress()) return current;

               if (current->left && current->left->maxEnd > address) current = current->left;
               else
                    current = current->right;
          }

          return nullptr;
     }

     VADNode* VirtualMemoryAllocator::Minimum(VADNode* node)
     {
          while (node->left) node = node->left;
          return node;
     }

     void VirtualMemoryAllocator::Transplant(VADNode* u, VADNode* v)
     {
          if (!u->parent) _root = v;
          else if (u == u->parent->left)
               u->parent->left = v;
          else
               u->parent->right = v;

          if (v) v->parent = u->parent;
     }

     void VirtualMemoryAllocator::FixDelete(VADNode* x, VADNode* xParent)
     {
          while (x != _root && (x == nullptr || x->colour == RBColour::Black))
          {
               if (x == (xParent ? xParent->left : nullptr))
               {
                    VADNode* w = xParent->right;

                    if (w && w->colour == RBColour::Red)
                    {
                         w->colour = RBColour::Black;
                         xParent->colour = RBColour::Red;
                         RotateLeft(xParent);
                         w = xParent->right;
                    }

                    if (w == nullptr)
                    {
                         x = xParent;
                         xParent = x->parent;
                    }
                    else if ((!w->left || w->left->colour == RBColour::Black) &&
                             (!w->right || w->right->colour == RBColour::Black))
                    {
                         w->colour = RBColour::Red;
                         x = xParent;
                         xParent = x->parent;
                    }
                    else
                    {
                         if (!w->right || w->right->colour == RBColour::Black)
                         {
                              if (w->left) w->left->colour = RBColour::Black;
                              w->colour = RBColour::Red;
                              RotateRight(w);
                              w = xParent->right;
                         }

                         w->colour = xParent->colour;
                         xParent->colour = RBColour::Black;
                         if (w->right) w->right->colour = RBColour::Black;
                         RotateLeft(xParent);
                         x = _root;
                    }
               }
               else
               {
                    VADNode* w = xParent->left;

                    if (w && w->colour == RBColour::Red)
                    {
                         w->colour = RBColour::Black;
                         xParent->colour = RBColour::Red;
                         RotateRight(xParent);
                         w = xParent->left;
                    }

                    if (w == nullptr)
                    {
                         x = xParent;
                         xParent = x->parent;
                    }
                    else if ((!w->right || w->right->colour == RBColour::Black) &&
                             (!w->left || w->left->colour == RBColour::Black))
                    {
                         w->colour = RBColour::Red;
                         x = xParent;
                         xParent = x->parent;
                    }
                    else
                    {
                         if (!w->left || w->left->colour == RBColour::Black)
                         {
                              if (w->right) w->right->colour = RBColour::Black;
                              w->colour = RBColour::Red;
                              RotateLeft(w);
                              w = xParent->left;
                         }
                         w->colour = xParent->colour;
                         xParent->colour = RBColour::Black;
                         if (w->left) w->left->colour = RBColour::Black;
                         RotateRight(xParent);
                         x = _root;
                    }
               }
          }

          if (x) x->colour = RBColour::Black;
     }

     bool VirtualMemoryAllocator::Remove(std::uintptr_t baseAddress)
     {
          VADNode* z = Search(baseAddress);
          if (z == nullptr)
          {
               debugging::DbgWrite(u8"[VirtualMemoryAllocator::Remove] Search(baseAddress) == nullptr\r\n");
               return false;
          }

          if (z->entry.state == VADMemoryState::Reserved || z->entry.state == VADMemoryState::Committed)
          {
               if (_stats.totalReservedBytes.load(std::memory_order::relaxed) >= z->entry.size)
                    _stats.totalReservedBytes.fetch_sub(z->entry.size, std::memory_order::relaxed);
          }

          if (z->entry.state == VADMemoryState::Committed)
          {
               if (_stats.totalCommittedBytes.load(std::memory_order::relaxed) >= z->entry.size)
                    _stats.totalCommittedBytes.fetch_sub(z->entry.size, std::memory_order::relaxed);
               if (_stats.commitCharge.load(std::memory_order::relaxed) >= z->entry.size)
                    _stats.commitCharge.fetch_sub(z->entry.size, std::memory_order::relaxed);
          }

          if (z->entry.immediatePhysical &&
              _stats.totalImmediateBytes.load(std::memory_order::relaxed) >= z->entry.size)
          {
               _stats.totalImmediateBytes.fetch_sub(z->entry.size, std::memory_order::relaxed);
          }

          _stats.releaseOperations.fetch_add(1, std::memory_order::relaxed);

          VADNode* y = z;
          VADNode* x = nullptr;
          RBColour yOriginalColour = y->colour;
          VADNode* xParent = nullptr;

          if (!z->left)
          {
               x = z->right;
               xParent = z->parent;
               Transplant(z, z->right);
          }
          else if (!z->right)
          {
               x = z->left;
               xParent = z->parent;
               Transplant(z, z->left);
          }
          else
          {
               y = Minimum(z->right);
               yOriginalColour = y->colour;
               x = y->right;
               xParent = y;

               if (y->parent == z)
               {
                    if (x) x->parent = y;
               }
               else
               {
                    xParent = y->parent;
                    Transplant(y, y->right);
                    y->right = z->right;
                    y->right->parent = y;
               }

               Transplant(z, y);
               y->left = z->left;
               y->left->parent = y;
               y->colour = z->colour;
               UpdateMaxEnd(y);
          }

          if (xParent) UpdateMaxEndUpwards(xParent);
          if (yOriginalColour == RBColour::Black) FixDelete(x, xParent);

          _nodeAllocator.FreeNode(z);
          return true;
     }

     void VirtualMemoryAllocator::InorderTraversal(VADNode* node, void (*callback)(const VADEntry&))
     {
          if (!node) return;
          InorderTraversal(node->left, callback);
          callback(node->entry);
          InorderTraversal(node->right, callback);
     }

     NO_ASAN bool VirtualMemoryAllocator::ReserveVirtualMemoryFixed(std::uintptr_t baseAddress, std::size_t size,
                                                                    MemoryProtection protection, PFNUse use)
     {
          size = AlignUp(size, PAGE_SIZE);
          VADEntry entry(baseAddress, size, VADMemoryState::Reserved, protection, use);
          return Insert(entry);
     }

     bool VirtualMemoryAllocator::ReserveVirtualMemoryFixedAsCommitted(std::uintptr_t baseAddress, std::size_t size,
                                                                       MemoryProtection protection, PFNUse use)
     {
          size = AlignUp(size, PAGE_SIZE);
          VADEntry entry(baseAddress, size, VADMemoryState::Committed, protection, use);
          return Insert(entry);
     }

     static constexpr std::size_t ALLOCATION_GAP = 0x100000;
     std::uintptr_t VirtualMemoryAllocator::FindFreeRegion(std::size_t size, std::uintptr_t hint)
     {
          std::uintptr_t searchAddress = hint > 0 ? hint : _searchStart;

          if (_root == nullptr) return searchAddress;

          VADNode* current = _root;
          while (current->left) current = current->left;

          std::uintptr_t candidateAddress = searchAddress;

          while (current)
          {
               if (candidateAddress + size <= current->entry.baseAddress) return candidateAddress;

               candidateAddress = AlignUp(std::max(candidateAddress, current->entry.baseAddress + current->entry.size) +
                                              ALLOCATION_GAP,
                                          ALLOCATION_GAP);
               if (current->right)
               {
                    current = current->right;
                    while (current->left) current = current->left;
               }
               else
               {
                    VADNode* parent = current->parent;
                    while (parent && current == parent->right)
                    {
                         current = parent;
                         parent = parent->parent;
                    }
                    current = parent;
               }
          }

          return candidateAddress;
     }

     bool VirtualMemoryAllocator::MapPhysicalPages(VADNode* node, std::uintptr_t baseAddress, std::size_t size)
     {
          const std::size_t numPages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
          const std::uintptr_t pageTable = _pageTableRoot != 0 ? _pageTableRoot : memory::paging::GetCurrentPageTable();

          for (std::size_t i = 0; i < numPages; i++)
          {
               std::uintptr_t physPage = physicalAllocator.AllocatePage(node->entry.use);
               if (physPage == ~0uz)
               {
                    _stats.failedAllocations.fetch_add(1, std::memory_order::relaxed);
                    debugging::DbgWrite(u8"[MapPhysicalPages] physicalAllocator.AllocatePage == ~0\r\n");
                    return false;
               }

               memory::PageMapping mapping{};
               mapping.virtualAddress = baseAddress + (i * PAGE_SIZE);
               mapping.physicalAddress = physPage;
               mapping.size = PAGE_SIZE;
               mapping.writable = (static_cast<std::uint16_t>(node->entry.protection) &
                                   static_cast<std::uint16_t>(MemoryProtection::ReadWrite)) != 0;
               mapping.userAccessible = baseAddress < 0x0000800000000000;
               mapping.cachePolicy = (static_cast<std::uint16_t>(node->entry.protection) &
                                      static_cast<std::uint16_t>(MemoryProtection::NoCache)) != 0
                                         ? memory::CachePolicy::Uncacheable
                                         : memory::CachePolicy::WriteBack;
               mapping.executable = (static_cast<std::uint16_t>(node->entry.protection) &
                                     static_cast<std::uint16_t>(MemoryProtection::Execute)) != 0;

               auto ptAllocator = [](std::size_t) -> void*
               {
                    std::uintptr_t page = physicalAllocator.AllocatePage(PFNUse::PageTable);
                    if (page == ~0uz) return nullptr;
                    return reinterpret_cast<void*>(page + memory::virtualOffset);
               };

               if (!memory::paging::MapPage(pageTable, mapping, ptAllocator))
               {
                    physicalAllocator.ReleaseFreePage(physPage);
                    _stats.failedAllocations.fetch_add(1, std::memory_order::relaxed);
                    debugging::DbgWrite(u8"[MapPhysicalPages] !memory::paging::MapPage\r\n");
                    return false;
               }

               memory::paging::InvalidatePage(mapping.virtualAddress);
          }

          return true;
     }

     bool VirtualMemoryAllocator::SplitVADForCommit(VADNode* node, std::uintptr_t commitStart, std::size_t commitSize)
     {
          const std::uintptr_t nodeStart = node->entry.baseAddress;
          const std::uintptr_t nodeEnd = node->EndAddress();
          const std::uintptr_t commitEnd = commitStart + commitSize;

          if (commitStart < nodeStart || commitEnd > nodeEnd)
          {
               debugging::DbgWrite(u8"[SplitVADForCommit] range [{}, {}) outside node [{}, {})\r\n",
                                   reinterpret_cast<void*>(commitStart), reinterpret_cast<void*>(commitEnd),
                                   reinterpret_cast<void*>(nodeStart), reinterpret_cast<void*>(nodeEnd));
               return false;
          }
          if (commitStart == nodeStart && commitEnd == nodeEnd) return true;

          const MemoryProtection prot = node->entry.protection;
          const PFNUse use = node->entry.use;

          if (!Remove(nodeStart))
          {
               debugging::DbgWrite(u8"[SplitVADForCommit] Remove(nodeStart) failed\r\n");
               return false;
          }

          VADEntry committedEntry(commitStart, commitSize, VADMemoryState::Reserved, prot, use, false);
          if (!Insert(committedEntry))
          {
               debugging::DbgWrite(u8"[SplitVADForCommit] Insert(committedEntry) failed — restoring\r\n");
               Insert(VADEntry(nodeStart, nodeEnd - nodeStart, VADMemoryState::Reserved, prot, use, false));
               return false;
          }

          if (commitStart == nodeStart)
          {
               if (commitEnd < nodeEnd)
               {
                    VADEntry backEntry(commitEnd, nodeEnd - commitEnd, VADMemoryState::Reserved, prot, use, false);
                    if (!Insert(backEntry))
                    {
                         debugging::DbgWrite(u8"[SplitVADForCommit:B] Insert(backEntry) failed\r\n");
                         Remove(commitStart);
                         Insert(VADEntry(nodeStart, nodeEnd - nodeStart, VADMemoryState::Reserved, prot, use, false));
                         return false;
                    }
               }
               return true;
          }

          if (commitEnd == nodeEnd)
          {
               VADEntry frontEntry(nodeStart, commitStart - nodeStart, VADMemoryState::Reserved, prot, use, false);
               if (!Insert(frontEntry))
               {
                    debugging::DbgWrite(u8"[SplitVADForCommit:C] Insert(frontEntry) failed\r\n");
                    Remove(commitStart);
                    Insert(VADEntry(nodeStart, nodeEnd - nodeStart, VADMemoryState::Reserved, prot, use, false));
                    return false;
               }
               return true;
          }

          VADEntry frontEntry(nodeStart, commitStart - nodeStart, VADMemoryState::Reserved, prot, use, false);
          if (!Insert(frontEntry))
          {
               debugging::DbgWrite(u8"[SplitVADForCommit:D] Insert(frontEntry) failed\r\n");
               Remove(commitStart);
               Insert(VADEntry(nodeStart, nodeEnd - nodeStart, VADMemoryState::Reserved, prot, use, false));
               return false;
          }

          VADEntry backEntry(commitEnd, nodeEnd - commitEnd, VADMemoryState::Reserved, prot, use, false);
          if (!Insert(backEntry))
          {
               debugging::DbgWrite(u8"[SplitVADForCommit:D] Insert(backEntry) failed\r\n");
               Remove(commitStart);
               Remove(nodeStart);
               Insert(VADEntry(nodeStart, nodeEnd - nodeStart, VADMemoryState::Reserved, prot, use, false));
               return false;
          }

          return true;
     }

     bool VirtualMemoryAllocator::CommitMemoryRange(VADNode* node, std::uintptr_t baseAddress, std::size_t size,
                                                    bool immediate)
     {
          if (node == nullptr)
          {
               debugging::DbgWrite(u8"[CommitMemoryRange] node == nullptr\r\n");
               return false;
          }

          if (baseAddress < node->entry.baseAddress || (baseAddress + size) > node->EndAddress())
          {
               debugging::DbgWrite(u8"[CommitMemoryRange] range out of bounds\r\n");
               return false;
          }

          if (node->entry.state == VADMemoryState::Committed)
          {
               debugging::DbgWrite(u8"[CommitMemoryRange] node [{}, {}) already Committed — idempotent ok\r\n",
                                   reinterpret_cast<void*>(node->entry.baseAddress),
                                   reinterpret_cast<void*>(node->EndAddress()));
               return true;
          }

          if (node->entry.state != VADMemoryState::Reserved)
          {
               debugging::DbgWrite(u8"[CommitMemoryRange] node->entry.state is neither Reserved nor Committed\r\n");
               return false;
          }

          const bool isExact = (baseAddress == node->entry.baseAddress) && (size == node->entry.size);
          if (!isExact)
          {
               if (!SplitVADForCommit(node, baseAddress, size))
               {
                    debugging::DbgWrite(u8"[CommitMemoryRange] SplitVADForCommit failed for [{}, {})\r\n",
                                        reinterpret_cast<void*>(baseAddress),
                                        reinterpret_cast<void*>(baseAddress + size));
                    return false;
               }

               node = FindContaining(baseAddress);
               if (!node)
               {
                    debugging::DbgWrite(u8"[CommitMemoryRange] FindContaining after split returned nullptr for {}\r\n",
                                        reinterpret_cast<void*>(baseAddress));
                    return false;
               }
          }

          if (immediate)
          {
               if (!MapPhysicalPages(node, baseAddress, size))
               {
                    const MemoryProtection prot = node->entry.protection;
                    const PFNUse use = node->entry.use;
                    std::uintptr_t spanStart = baseAddress;
                    std::uintptr_t spanEnd = baseAddress + size;

                    if (baseAddress > 0)
                    {
                         VADNode* prev = FindContaining(baseAddress - 1);
                         if (prev && prev->entry.state == VADMemoryState::Reserved && prev->EndAddress() == baseAddress)
                         {
                              spanStart = prev->entry.baseAddress;
                              Remove(spanStart);
                         }
                    }

                    VADNode* next = FindContaining(spanEnd);
                    if (next && next->entry.state == VADMemoryState::Reserved && next->entry.baseAddress == spanEnd)
                    {
                         spanEnd = next->EndAddress();
                         Remove(next->entry.baseAddress);
                    }

                    Remove(baseAddress);
                    Insert(VADEntry(spanStart, spanEnd - spanStart, VADMemoryState::Reserved, prot, use, false));
                    return false;
               }
               _stats.totalImmediateBytes.fetch_add(size, std::memory_order::relaxed);
               node->entry.immediatePhysical = true;
          }

          node->entry.state = VADMemoryState::Committed;

          const std::size_t newCommitted =
              _stats.totalCommittedBytes.fetch_add(size, std::memory_order::relaxed) + size;
          _stats.commitCharge.fetch_add(size, std::memory_order::relaxed);

          std::size_t currentPeak = _stats.peakCommittedBytes.load(std::memory_order::relaxed);
          while (newCommitted > currentPeak && !_stats.peakCommittedBytes.compare_exchange_weak(
                                                   currentPeak, newCommitted, std::memory_order::relaxed))
          {
          }

          _stats.commitOperations.fetch_add(1, std::memory_order::relaxed);
          return true;
     }

     bool VirtualMemoryAllocator::SplitVADForDecommit(VADNode* node, std::uintptr_t decommitStart,
                                                      std::size_t decommitSize)
     {
          if (node == nullptr)
          {
               debugging::DbgWrite(u8"[SplitVADForDecommit] node == nullptr\r\n");
               return false;
          }

          const std::uintptr_t nodeStart = node->entry.baseAddress;
          const std::uintptr_t nodeEnd = node->EndAddress();
          const std::uintptr_t decommitEnd = decommitStart + decommitSize;

          if (decommitStart < nodeStart || decommitEnd > nodeEnd)
          {
               debugging::DbgWrite(u8"[SplitVADForDecommit] decommitStart < nodeStart || decommitEnd > nodeEnd\r\n");
               return false;
          }

          if (node->entry.state != VADMemoryState::Committed)
          {
               debugging::DbgWrite(u8"[SplitVADForDecommit] node->entry.state != VADMemoryState::Committed\r\n");
               return false;
          }

          if (node->entry.immediatePhysical)
          {
               const std::size_t numPages = (decommitSize + PAGE_SIZE - 1) / PAGE_SIZE;
               const std::uintptr_t pageTable =
                   _pageTableRoot != 0 ? _pageTableRoot : memory::paging::GetCurrentPageTable();

               for (std::size_t i = 0; i < numPages; i++)
               {
                    const std::uintptr_t virtualAddr = decommitStart + (i * PAGE_SIZE);

                    auto* pml4 = reinterpret_cast<std::uint64_t*>(pageTable);
                    const std::uint64_t pml4Index = (virtualAddr >> 39) & 0x1FF;
                    const std::uint64_t pdptIndex = (virtualAddr >> 30) & 0x1FF;
                    const std::uint64_t pdIndex = (virtualAddr >> 21) & 0x1FF;
                    const std::uint64_t ptIndex = (virtualAddr >> 12) & 0x1FF;

                    if ((pml4[pml4Index] & 1) != 0)
                    {
                         auto* pdpt = reinterpret_cast<std::uint64_t*>(pml4[pml4Index] & ~0xFFFULL);
                         if ((pdpt[pdptIndex] & 1) != 0)
                         {
                              auto* pd = reinterpret_cast<std::uint64_t*>(pdpt[pdptIndex] & ~0xFFFULL);
                              if ((pd[pdIndex] & 1) != 0 && (pd[pdIndex] & (1ULL << 7)) == 0)
                              {
                                   auto* pt = reinterpret_cast<std::uint64_t*>(pd[pdIndex] & ~0xFFFULL);
                                   if ((pt[ptIndex] & 1) != 0)
                                   {
                                        const std::uintptr_t physAddr = pt[ptIndex] & ~0xFFFULL;
                                        pt[ptIndex] = 0;
                                        physicalAllocator.ReleaseFreePage(physAddr);
                                        memory::paging::InvalidatePage(virtualAddr);
                                   }
                              }
                         }
                    }
               }
               _stats.totalImmediateBytes.fetch_sub(decommitSize, std::memory_order::relaxed);
          }

          if (_stats.totalCommittedBytes.load(std::memory_order::relaxed) >= decommitSize)
               _stats.totalCommittedBytes.fetch_sub(decommitSize, std::memory_order::relaxed);
          if (_stats.commitCharge.load(std::memory_order::relaxed) >= decommitSize)
               _stats.commitCharge.fetch_sub(decommitSize, std::memory_order::relaxed);

          _stats.decommitOperations.fetch_add(1, std::memory_order::relaxed);

          if (decommitStart == nodeStart && decommitEnd == nodeEnd)
          {
               node->entry.state = VADMemoryState::Reserved;
               node->entry.immediatePhysical = false;
               return true;
          }

          if (decommitStart == nodeStart)
          {
               const std::uintptr_t newStart = decommitEnd;
               const std::size_t newSize = nodeEnd - newStart;

               VADEntry decommittedEntry(nodeStart, decommitSize, VADMemoryState::Reserved, node->entry.protection,
                                         node->entry.use, false);

               node->entry.baseAddress = newStart;
               node->entry.size = newSize;

               if (!Insert(decommittedEntry))
               {
                    debugging::DbgWrite(u8"[SplitVADForDecommit:1] Insert failed {}\r\n", decommittedEntry);
                    return false;
               }

               return true;
          }

          if (decommitEnd == nodeEnd)
          {
               node->entry.size = decommitStart - nodeStart;
               UpdateMaxEndUpwards(node);

               VADEntry decommittedEntry(decommitStart, decommitSize, VADMemoryState::Reserved, node->entry.protection,
                                         node->entry.use, false);
               return Insert(decommittedEntry);
          }

          const std::size_t frontSize = decommitStart - nodeStart;
          node->entry.size = frontSize;
          UpdateMaxEndUpwards(node);

          VADEntry decommittedEntry(decommitStart, decommitSize, VADMemoryState::Reserved, node->entry.protection,
                                    node->entry.use, false);
          if (!Insert(decommittedEntry))
          {
               debugging::DbgWrite(u8"[SplitVADForDecommit:2] !Insert({})\r\n", decommittedEntry);
               return false;
          }

          VADEntry backEntry(decommitEnd, nodeEnd - decommitEnd, VADMemoryState::Committed, node->entry.protection,
                             node->entry.use, node->entry.immediatePhysical);
          if (!Insert(backEntry))
          {
               Remove(decommitStart);
               debugging::DbgWrite(u8"[SplitVADForDecommit] !Insert(backEntry)\r\n");
               return false;
          }

          node->entry.size = frontSize;
          UpdateMaxEndUpwards(node);
          return true;
     }

     bool VirtualMemoryAllocator::DecommitMemoryRange(VADNode* node, std::uintptr_t baseAddress, std::size_t size)
     {
          if (node == nullptr)
          {
               debugging::DbgWrite(u8"[DecommitMemoryRange] node == nullptr\r\n");
               return false;
          }

          if (baseAddress < node->entry.baseAddress || (baseAddress + size) > node->EndAddress())
          {
               debugging::DbgWrite(u8"[DecommitMemoryRange] baseAddress < node->entry.baseAddress || "
                                   u8"(baseAddress + size) > node->EndAddress()\r\n");
               return false;
          }

          if (node->entry.state != VADMemoryState::Committed)
          {
               debugging::DbgWrite(u8"[DecommitMemoryRange] node->entry.state != VADMemoryState::Committed\r\n");
               return false;
          }

          return SplitVADForDecommit(node, baseAddress, size);
     }

     NO_ASAN std::uintptr_t VirtualMemoryAllocator::ReserveVirtualMemory(std::size_t size, MemoryProtection protection,
                                                                         PFNUse use)
     {
          size = AlignUp(size, PAGE_SIZE);

          AcquireLock();

          std::uintptr_t baseAddress = FindFreeRegion(size, 0);
          if (baseAddress == 0)
          {
               _stats.failedAllocations.fetch_add(1, std::memory_order::relaxed);
               ReleaseLock();
               return 0;
          }

          if (ReserveVirtualMemoryFixed(baseAddress, size, protection, use))
          {
               _stats.reserveOperations.fetch_add(1, std::memory_order::relaxed);
               const std::size_t newReserved =
                   _stats.totalReservedBytes.fetch_add(size, std::memory_order::relaxed) + size;
               std::size_t currentPeak = _stats.peakReservedBytes.load(std::memory_order::relaxed);
               while (newReserved > currentPeak && !_stats.peakReservedBytes.compare_exchange_weak(
                                                       currentPeak, newReserved, std::memory_order::relaxed))
               {
               }
               ReleaseLock();
               return baseAddress;
          }

          _stats.failedAllocations.fetch_add(1, std::memory_order::relaxed);
          ReleaseLock();
          return 0;
     }

     NO_ASAN std::uintptr_t VirtualMemoryAllocator::ReserveVirtualMemoryAt(std::uintptr_t hint, std::size_t size,
                                                                           MemoryProtection protection, PFNUse use)
     {
          size = AlignUp(size, PAGE_SIZE);

          AcquireLock();

          std::uintptr_t baseAddress = FindFreeRegion(size, hint);
          if (baseAddress == 0)
          {
               _stats.failedAllocations.fetch_add(1, std::memory_order::relaxed);
               ReleaseLock();
               return 0;
          }

          if (ReserveVirtualMemoryFixed(baseAddress, size, protection, use))
          {
               _stats.reserveOperations.fetch_add(1, std::memory_order::relaxed);
               const std::size_t newReserved =
                   _stats.totalReservedBytes.fetch_add(size, std::memory_order::relaxed) + size;
               std::size_t currentPeak = _stats.peakReservedBytes.load(std::memory_order::relaxed);
               while (newReserved > currentPeak && !_stats.peakReservedBytes.compare_exchange_weak(
                                                       currentPeak, newReserved, std::memory_order::relaxed))
               {
               }
               ReleaseLock();
               return baseAddress;
          }

          _stats.failedAllocations.fetch_add(1, std::memory_order::relaxed);
          ReleaseLock();
          return 0;
     }

     NO_ASAN bool VirtualMemoryAllocator::CommitMemory(std::uintptr_t baseAddress, std::size_t size, bool immediate)
     {
          if (size == 0)
          {
               debugging::DbgWrite(u8"[CommitMemory] size == 0\r\n");
               return false;
          }

          AcquireLock();

          VADNode* node = FindContaining(baseAddress);
          if (node == nullptr)
          {
               debugging::DbgWrite(u8"[CommitMemory] FindContaining({}) == nullptr\r\n",
                                   reinterpret_cast<void*>(baseAddress));
               ReleaseLock();
               return false;
          }

          const bool result = CommitMemoryRange(node, baseAddress, size, immediate);
          ReleaseLock();
          return result;
     }

     bool VirtualMemoryAllocator::DecommitMemory(std::uintptr_t baseAddress, std::size_t size)
     {
          if (size == 0)
          {
               debugging::DbgWrite(u8"[DecommitMemory] size == 0\r\n");
               return false;
          }

          AcquireLock();

          VADNode* node = FindContaining(baseAddress);
          if (node == nullptr)
          {
               debugging::DbgWrite(u8"[DecommitMemory] FindContaining({}) == nullptr\r\n",
                                   reinterpret_cast<void*>(baseAddress));
               ReleaseLock();
               return false;
          }

          const bool result = DecommitMemoryRange(node, baseAddress, size);
          ReleaseLock();
          return result;
     }

     [[nodiscard]] std::uintptr_t KeGetPhysicalAddress(void* virtualAddress)
     {
#ifdef ARCH_X8664
          const std::uintptr_t va = reinterpret_cast<std::uintptr_t>(virtualAddress);
          const std::uintptr_t pageTable = paging::GetCurrentPageTable();

          auto* pml4 = reinterpret_cast<std::uint64_t*>(pageTable + virtualOffset);
          const std::uint64_t pml4Index = (va >> 39) & 0x1FF;
          const std::uint64_t pdptIndex = (va >> 30) & 0x1FF;
          const std::uint64_t pdIndex = (va >> 21) & 0x1FF;
          const std::uint64_t ptIndex = (va >> 12) & 0x1FF;

          if ((pml4[pml4Index] & 1) == 0) return ~0uz;
          auto* pdpt = reinterpret_cast<std::uint64_t*>((pml4[pml4Index] & ~0xFFFULL) + virtualOffset);

          if ((pdpt[pdptIndex] & 1) == 0) return ~0uz;
          auto* pd = reinterpret_cast<std::uint64_t*>((pdpt[pdptIndex] & ~0xFFFULL) + virtualOffset);

          if ((pd[pdIndex] & 1) == 0 || (pd[pdIndex] & (1ULL << 7)) != 0) return ~0uz;
          auto* pt = reinterpret_cast<std::uint64_t*>((pd[pdIndex] & ~0xFFFULL) + virtualOffset);

          if ((pt[ptIndex] & 1) == 0) return ~0uz;
          return (pt[ptIndex] & ~0xFFFULL) + (va & 0xFFF);
#elifdef ARCH_ARM64
          const std::uintptr_t va = reinterpret_cast<std::uintptr_t>(virtualAddress);
          const std::uintptr_t pageTable = paging::GetCurrentPageTable();

          auto* pgd = reinterpret_cast<std::uint64_t*>(pageTable);
          const std::uint64_t pgdIndex = (va >> 39) & 0x1FF;
          const std::uint64_t pudIndex = (va >> 30) & 0x1FF;
          const std::uint64_t pmdIndex = (va >> 21) & 0x1FF;
          const std::uint64_t ptIndex = (va >> 12) & 0x1FF;

          if ((pgd[pgdIndex] & 1) == 0) return ~0uz;
          auto* pud = reinterpret_cast<std::uint64_t*>(pgd[pgdIndex] & ~0xFFFULL);

          if ((pud[pudIndex] & 1) == 0) return ~0uz;
          auto* pmd = reinterpret_cast<std::uint64_t*>(pud[pudIndex] & ~0xFFFULL);

          if ((pmd[pmdIndex] & 1) == 0 || (pmd[pmdIndex] & (1ULL << 7)) != 0) return ~0uz;
          auto* pt = reinterpret_cast<std::uint64_t*>(pmd[pmdIndex] & ~0xFFFULL);

          if ((pt[ptIndex] & 1) == 0) return ~0uz;
          return (pt[ptIndex] & ~0xFFFULL) + (va & 0xFFF);
#else
#error "Unsupported architecture"
#endif
     }
} // namespace memory

template <> struct debugging::SerialFormatter<memory::VADEntry>
{
     static void Write(const memory::VADEntry& entry, const FormatSpec& spec)
     {
          debugging::SerialFormatter<char8_t[sizeof(u8"VADEntry{ .baseAddress = ")]>::Write( // NOLINT
              u8"VADEntry{ .baseAddress = ", spec);
          debugging::SerialFormatter<const volatile void*>::Write(reinterpret_cast<void*>(entry.baseAddress), spec);
          debugging::SerialFormatter<char8_t[sizeof(u8"}")]>::Write(u8"}", spec); // NOLINT
     }
};
