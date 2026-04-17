#include "dpc.h"
#include "../process/taskScheduler.h"

bool device::KeInsertQueueDpc(DPC* dpc, void* argument)
{
     if (!dpc) return false;
     dpc->argument = argument;

     auto* cpu = process::KeCurrentCpu();
     cpu->dpcQueue.Push(new structures::SingleListEntry<DPC>{.next = nullptr, .data = *dpc});
     return true;
}

void device::KeFlushQueuedDpcs()
{
     auto* cpu = process::KeCurrentCpu();
     auto* entry = cpu->dpcQueue.Pop();
     while (entry)
     {
          auto& dpc = entry->data;
          dpc.routine(&dpc);
          entry = cpu->dpcQueue.Pop();
     }
}
