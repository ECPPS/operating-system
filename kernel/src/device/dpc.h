#pragma once

#include <utils/struct.h>
#include <cstdint>

namespace device
{
     struct DPC
     {
          std::uint8_t type{};
          std::uint8_t importance{};
          void (*routine)(DPC* dpc){};
          void* context{};
          void* argument{};
     };

     bool KeInsertQueueDpc(DPC* dpc, void* argument);
     void KeFlushQueuedDpcs();
} // namespace device
