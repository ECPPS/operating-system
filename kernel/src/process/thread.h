#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include "process.h"

namespace kernel
{
     struct ProcessControlBlock;
}

namespace process
{
     constexpr std::uint64_t ThreadStackSize = 1024ULL * 1024ULL; // 1 MiB

     enum struct ThreadState : std::uint8_t
     {
          Created,
          Running,
          Runnable,
          Finished,
          Sleeping,
          DeletedWaitingForRelease,
          EventWait,
          InterruptWait
     };

     enum struct ThreadPriority : std::uint8_t
     {
          Idle,
          Low,
          BelowNormal,
          Normal,
          High,
          Realtime,
          Count_,
     };

     constexpr std::array<int, static_cast<std::size_t>(ThreadPriority::Count_)> ThreadPriorityQuantum{2,  4,  8,
                                                                                                       12, 16, 24};

     using ThreadRoutine = void (*)(void*);

     struct Thread
     {
          std::uint64_t id{};
          void* stackPointer{};
          void* stackBase{};
          Thread* next{};
          ThreadState state{};
          ThreadPriority priority{};
          kernel::ProcessControlBlock* parentProcess{};
          std::uintptr_t kernelRsp{};
          int quantumCounter{};
          std::uint64_t argument{};
          std::uint64_t stackSize{ThreadStackSize};

          Thread(const Thread&) = delete;
          Thread(Thread&&) = default;
          Thread& operator=(const Thread&) = delete;
          Thread& operator=(Thread&&) = default;
          Thread() = default;
     };
};
