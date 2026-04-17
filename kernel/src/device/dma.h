#pragma once

#include <cstddef>
#include <cstdint>
namespace dma
{
     struct DMABuffer
     {
          void* virt;
          std::uintptr_t phys;
          std::size_t size;
     };

     DMABuffer KeDMAAllocate(std::size_t size);
     void KeDMAFree(const DMABuffer& buffer);
} // namespace dma
