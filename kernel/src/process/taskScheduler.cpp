#include "taskScheduler.h"
#include <utils/identify.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <utility>
#include "../dbg/kasan.h"
#include "../kinit.h"
#include "thread.h"
#include "utils/kdbg.h"
#include "utils/memory.h"
#include "utils/operations.h"

static std::atomic<bool> g_schedulerInitialised{false};
static std::atomic<std::uint64_t> g_schedulerGSBaseMask{0};

[[nodiscard]] bool TryAcquireSelectionLock(process::CpuLocal* cpu) noexcept
{
     bool expected = false;
     for (int attempt = 0; attempt < 64; ++attempt)
     {
          if (cpu->selectionLock.compare_exchange_weak(expected, true, std::memory_order::acquire,
                                                       std::memory_order::relaxed))
               return true;

          expected = false;
          operations::Yield();
     }
     return false;
}

void AcquireSelectionLock(process::CpuLocal* cpu) noexcept
{
     bool expected = false;
     while (!cpu->selectionLock.compare_exchange_weak(expected, true, std::memory_order::acquire,
                                                      std::memory_order::relaxed))
     {
          expected = false;
          operations::Yield();
     }
}

void ReleaseSelectionLock(process::CpuLocal* cpu) noexcept
{
     cpu->selectionLock.store(false, std::memory_order::release);
}

static std::array<process::CpuLocal*, 256> g_cpuLocalArray{};
static std::atomic<std::size_t> g_cpuCount{0};

namespace
{
     [[nodiscard]] constexpr bool IsThreadRunnable(const process::Thread* thread) noexcept
     {
          if (!thread) return false;
          return thread->state == process::ThreadState::Runnable || thread->state == process::ThreadState::Running;
     }

     [[nodiscard]] process::Thread* FindHighestPriorityThread(process::CpuLocal* cpu) noexcept
     {
          for (int i = static_cast<int>(process::ThreadPriority::Count_) - 1; i >= 0; --i)
          {
               process::Thread* head = cpu->readyQueues[i];
               if (head && IsThreadRunnable(head)) return head;
          }
          return nullptr;
     }

     void RemoveFromReadyQueue(process::CpuLocal* cpu, process::Thread* thread) noexcept
     {
          if (!thread) return;

          const auto index = static_cast<std::size_t>(thread->priority);
          process::Thread** head = &cpu->readyQueues[index];
          if (!*head) return;

          if (*head == thread)
          {
               *head = thread->next;
               thread->next = nullptr;
               return;
          }

          process::Thread* previous = *head;
          while (previous->next && previous->next != thread) previous = previous->next;

          if (previous->next == thread)
          {
               previous->next = thread->next;
               thread->next = nullptr;
          }
     }

     void AddToReadyQueue(process::CpuLocal* cpu, process::Thread* thread) noexcept
     {
          if (!thread) return;

          const auto index = static_cast<std::size_t>(thread->priority);
          process::Thread** head = &cpu->readyQueues[index];

          thread->next = nullptr;
          if (!*head)
          {
               *head = thread;
               return;
          }

          process::Thread* tail = *head;
          while (tail->next) tail = tail->next;
          tail->next = thread;
     }

     void ResetQuantum(process::Thread* thread) noexcept
     {
          if (!thread) return;
          const auto index = static_cast<std::size_t>(thread->priority);
          thread->quantumCounter = process::ThreadPriorityQuantum[index];
     }

     [[nodiscard]] int HighestOccupiedPriority(const process::CpuLocal* cpu) noexcept
     {
          for (int i = static_cast<int>(process::ThreadPriority::Count_) - 1; i >= 0; --i)
          {
               if (static_cast<process::ThreadPriority>(i) == process::ThreadPriority::Idle) continue;
               if (cpu->readyQueues[i]) return i;
          }
          return -1;
     }

     [[nodiscard]] process::Thread* StealFromQueue(process::CpuLocal* victim, int priorityIndex) noexcept
     {
          process::Thread** head = &victim->readyQueues[priorityIndex];
          if (!*head) return nullptr;

          process::Thread* stolen = *head;
          if (stolen->state != process::ThreadState::Runnable) return nullptr;

          *head = stolen->next;
          stolen->next = nullptr;
          return stolen;
     }
} // namespace

namespace
{
     struct SleepEntry
     {
          process::Thread* thread{};
          std::uint64_t wakeupTimeMs{};
          SleepEntry* next{};
     };

     struct SleepQueue
     {
          SleepEntry* head{};
          std::atomic<bool> lock{false};
     };

     SleepQueue g_sleepQueue{};

     void AcquireSleepQueueLock() noexcept
     {
          bool expected = false;
          while (!g_sleepQueue.lock.compare_exchange_weak(expected, true, std::memory_order::acquire,
                                                          std::memory_order::relaxed))
          {
               expected = false;
               operations::Yield();
          }
     }

     void ReleaseSleepQueueLock() noexcept { g_sleepQueue.lock.store(false, std::memory_order::release); }

     void ProcessSleepQueue(process::CpuLocal* cpu) noexcept
     {
          const std::uint64_t now = KeCurrentSystemTime();

          AcquireSleepQueueLock();

          while (g_sleepQueue.head && g_sleepQueue.head->wakeupTimeMs <= now)
          {
               SleepEntry* entry = g_sleepQueue.head;
               g_sleepQueue.head = entry->next;

               process::Thread* thread = entry->thread;
               delete entry;

               if (!thread) continue;

               const auto threadState = std::atomic_ref(thread->state).load(std::memory_order::acquire);

               if (threadState == process::ThreadState::Sleeping)
               {

                    AcquireSelectionLock(cpu);
                    std::atomic_ref(thread->state).store(process::ThreadState::Runnable, std::memory_order::release);
                    AddToReadyQueue(cpu, thread);
                    ReleaseSelectionLock(cpu);
               }
          }

          ReleaseSleepQueueLock();
     }
} // namespace

[[nodiscard]] process::Thread* KiStealThread(process::CpuLocal* thief) noexcept
{
     const std::size_t count = g_cpuCount.load(std::memory_order::acquire);

     process::CpuLocal* bestVictim = nullptr;
     int bestPriority = -1;

     for (std::size_t i = 0; i < count; ++i)
     {

          process::CpuLocal* candidate = std::atomic_ref(g_cpuLocalArray[i]).load(std::memory_order::acquire);
          if (!candidate || candidate == thief) continue;
          if (candidate->isSleeping) continue;

          for (int p = static_cast<int>(process::ThreadPriority::Count_) - 1; p > bestPriority; --p)
          {
               if (static_cast<process::ThreadPriority>(p) == process::ThreadPriority::Idle) continue;
               if (candidate->readyQueues[p])
               {
                    bestPriority = p;
                    bestVictim = candidate;
                    break;
               }
          }
     }

     if (!bestVictim) return nullptr;

     if (!TryAcquireSelectionLock(bestVictim)) return nullptr;

     const int confirmedPriority = HighestOccupiedPriority(bestVictim);
     process::Thread* stolen = nullptr;
     if (confirmedPriority >= 0) stolen = StealFromQueue(bestVictim, confirmedPriority);

     ReleaseSelectionLock(bestVictim);
     return stolen;
}

extern volatile std::uint32_t* g_lapic;

process::CpuLocal* process::KeCurrentCpu()
{
     if (!g_schedulerInitialised.load(std::memory_order::acquire)) return nullptr;
     const std::uint64_t cpuId = g_lapic[0x20 / 4] >> 24;
     const auto mask = g_schedulerGSBaseMask.load(std::memory_order::acquire);
     if ((mask & (1uz << cpuId)) == 0) return nullptr;

#ifdef ARCH_X8664
#ifdef COMPILER_MSVC
     CpuLocal* cpuLocal{};
     __readgsbase(&cpuLocal);
     return cpuLocal;
#else
     CpuLocal* cpuLocal{};
     asm("mov %%gs:0, %0" : "=r"(cpuLocal));
     return cpuLocal;
#endif
#elifdef ARCH_ARM64
#ifdef COMPILER_MSVC
     CpuLocal* cpuLocal{};
     __readtp(&cpuLocal);
     return cpuLocal;
#else
     CpuLocal* cpuLocal{};
     asm("mrs %0, tpidr_el1" : "=r"(cpuLocal));
     return cpuLocal;
#endif
#endif
}

void KiSetCpuLocal(process::CpuLocal* cpuLocal)
{
#ifdef ARCH_X8664
#ifdef COMPILER_MSVC
     __writemsr(0xC000'0101, reinterpret_cast<std::uintptr_t>(cpuLocal));
     __writemsr(0xC000'0102, reinterpret_cast<std::uintptr_t>(cpuLocal));
#else
     const std::uintptr_t value = reinterpret_cast<std::uintptr_t>(cpuLocal);
     asm volatile("wrmsr"
                  :
                  : "c"(0xC0000101u), "a"(static_cast<std::uint32_t>(value)),
                    "d"(static_cast<std::uint32_t>(value >> 32))
                  : "memory");
     asm volatile("wrmsr"
                  :
                  : "c"(0xC0000102u), "a"(static_cast<std::uint32_t>(value)),
                    "d"(static_cast<std::uint32_t>(value >> 32))
                  : "memory");
#endif
#elifdef ARCH_ARM64
#ifdef COMPILER_MSVC
     _WriteStatusReg(ARM64_SYSREG(3, 0, 13, 0, 4), reinterpret_cast<std::uintptr_t>(cpuLocal));
#else
     const std::uintptr_t value = reinterpret_cast<std::uintptr_t>(cpuLocal);
     asm volatile("msr tpidr_el1, %0" : : "r"(value) : "memory");
#endif
#endif
}

kernel::ProcessControlBlock* process::KeCurrentProcess()
{
     auto* cpuLocal = KeCurrentCpu();
     if (!cpuLocal) return nullptr;
     auto* thread = cpuLocal->thread;
     return thread ? thread->parentProcess : nullptr;
}

process::Thread* process::KeCurrentThread()
{
     auto* cpuLocal = KeCurrentCpu();
     return cpuLocal ? cpuLocal->thread : nullptr;
}

std::uint64_t process::KiAllocateThreadId()
{
     static std::atomic<std::uint64_t> nextThreadId{1};
     return nextThreadId.fetch_add(1, std::memory_order::relaxed);
}

//

static process::Thread* KiPsFindNextThread()
{
     auto* currentCpu = process::KeCurrentCpu();
     if (!currentCpu) return nullptr;
     auto* currentThread = currentCpu->thread;
     if (!currentThread) return nullptr;

     ProcessSleepQueue(currentCpu);

     AcquireSelectionLock(currentCpu);

     if (currentThread->quantumCounter > 0 && IsThreadRunnable(currentThread))
     {
          currentThread->quantumCounter--;
          ReleaseSelectionLock(currentCpu);
          return currentThread;
     }

     if (currentThread->state == process::ThreadState::Running)
     {
          currentThread->state = process::ThreadState::Runnable;
          AddToReadyQueue(currentCpu, currentThread);
     }
     else if (currentThread->state == process::ThreadState::Finished ||
              currentThread->state == process::ThreadState::DeletedWaitingForRelease)
     {
          RemoveFromReadyQueue(currentCpu, currentThread);
     }

     process::Thread* nextThread = FindHighestPriorityThread(currentCpu);

     if (!nextThread || nextThread == currentCpu->idleThread)
     {
          ReleaseSelectionLock(currentCpu);

          process::Thread* stolen = KiStealThread(currentCpu);

          if (stolen)
          {
               AcquireSelectionLock(currentCpu);
               AddToReadyQueue(currentCpu, stolen);
               nextThread = FindHighestPriorityThread(currentCpu);
               ReleaseSelectionLock(currentCpu);
          }

          if (!nextThread)
          {
               nextThread = currentCpu->idleThread;
               if (nextThread)
               {
                    std::atomic_ref(nextThread->state).store(process::ThreadState::Running, std::memory_order::release);
                    ResetQuantum(nextThread);
               }
               return nextThread;
          }

          AcquireSelectionLock(currentCpu);
     }

     RemoveFromReadyQueue(currentCpu, nextThread);
     ReleaseSelectionLock(currentCpu);

     std::atomic_ref(nextThread->state).store(process::ThreadState::Running, std::memory_order::release);
     ResetQuantum(nextThread);
     return nextThread;
}

extern std::atomic<bool> haltInProgress;

#ifdef ARCH_X8664
struct InterruptFrame
{
     std::uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
     std::uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
     std::uint64_t vector, errorCode;
     std::uint64_t rip, cs, rflags, rsp, ss;
};
#elifdef ARCH_ARM64
struct InterruptFrame
{
     std::uint64_t x0, x1, x2, x3, x4, x5, x6, x7;
     std::uint64_t x8, x9, x10, x11, x12, x13, x14, x15;
     std::uint64_t x16, x17, x18;
     std::uint64_t x19, x20, x21, x22, x23, x24, x25, x26, x27, x28;
     std::uint64_t fp, lr;
     std::uint64_t vector, esr, far, svc, sp;
};
#endif

void* process::KiSwitchThread(void* lpRegisters)
{
     if (!g_schedulerInitialised.load(std::memory_order::acquire)) return lpRegisters;

     auto* currentCpu = KeCurrentCpu();
     if (!currentCpu) return lpRegisters;
     Thread* currentThread = currentCpu->thread;
     if (!currentThread) return lpRegisters;

     currentThread->stackPointer = lpRegisters;

     auto* nextThread = KiPsFindNextThread();
     if (!nextThread) return lpRegisters;

     currentCpu->thread = nextThread;

     std::atomic_thread_fence(std::memory_order::acquire);

     auto* nextStackPointer = static_cast<std::uint8_t*>(nextThread->stackPointer);
     std::atomic_ref(nextThread->state).store(ThreadState::Running, std::memory_order::release);

     InterruptFrame* frame = reinterpret_cast<InterruptFrame*>(nextStackPointer);

     return nextStackPointer;
}

static NO_ASAN void KiInitialiseThreadStacks(void* stackPointer, bool isUsermode, void* entryPoint, void* parameter)
{
     auto* stack = static_cast<InterruptFrame*>(stackPointer);
#ifdef ARCH_X8664
     stack->rip = reinterpret_cast<std::uintptr_t>(entryPoint);
     stack->cs = isUsermode ? 0x1Bu : 0x08u;
     stack->rflags = 0x202u;
     stack->rsp = reinterpret_cast<std::uintptr_t>(stackPointer) + sizeof(InterruptFrame) + 4096;
     stack->rbp = 0;
     stack->ss = isUsermode ? 0x23u : 0x10u;
     stack->rcx = reinterpret_cast<std::uintptr_t>(parameter);
#elifdef ARCH_ARM64
     stack->lr = reinterpret_cast<std::uintptr_t>(entryPoint);
     stack->sp = reinterpret_cast<std::uintptr_t>(stackPointer) + sizeof(InterruptFrame) + 4096;
     stack->x0 = reinterpret_cast<std::uintptr_t>(parameter);
#endif
}

process::Thread* process::KiInitialiseTaskScheduler(void* idleProcedure, std::uintptr_t stackPointer, bool isBSP)
{
     if (isBSP) std::memset(g_cpuLocalArray.data(), 0, sizeof(g_cpuLocalArray));

     const std::uint64_t cpuId = g_lapic[0x20 / 4] >> 24;

     auto* cpuLocal = new CpuLocal{};
     Thread* kernelThread = new Thread{};

     kernelThread->id = KiAllocateThreadId();
     kernelThread->stackBase = reinterpret_cast<void*>(stackPointer);
     kernelThread->stackPointer = nullptr;
     kernelThread->state = ThreadState::Running;
     kernelThread->priority = ThreadPriority::Normal;
     kernelThread->parentProcess = g_kernelProcess;
     kernelThread->next = nullptr;

     cpuLocal->thread = kernelThread;
     cpuLocal->self = cpuLocal;
     cpuLocal->cpuId = cpuId;
     cpuLocal->isBSP = isBSP;
     cpuLocal->kernelRSP = stackPointer;
     cpuLocal->userRSP = 0;
     cpuLocal->idleThread = nullptr;
     cpuLocal->isSleeping = false;
     cpuLocal->irql = cpu::IRQL::Passive;
     cpuLocal->lastTSC = 0;
     cpuLocal->selectionLock.store(false, std::memory_order::relaxed);
     cpuLocal->canReschedule.store(true, std::memory_order::relaxed);

     for (auto& queue : cpuLocal->readyQueues) queue = nullptr;

     Thread* idleThread = new Thread{};
     idleThread->id = KiAllocateThreadId();

     idleThread->stackBase = static_cast<std::uint8_t*>(
         g_kernelProcess->AllocateVirtualMemory(nullptr, process::ThreadStackSize,
                                                memory::AllocationFlags::Commit | memory::AllocationFlags::Reserve |
                                                    memory::AllocationFlags::ImmediatePhysical,
                                                memory::MemoryProtection::ReadWrite));
     std::memset(idleThread->stackBase, 0xcc, process::ThreadStackSize);

     KASANAllocateHeap(idleThread->stackBase, process::ThreadStackSize);

     idleThread->stackPointer =
         static_cast<std::byte*>(idleThread->stackBase) + process::ThreadStackSize - sizeof(InterruptFrame) - 4096;
     idleThread->state = ThreadState::Runnable;
     idleThread->priority = ThreadPriority::Idle;
     idleThread->parentProcess = nullptr;
     idleThread->next = nullptr;

     cpuLocal->idleThread = idleThread;
     KiInitialiseThreadStacks(idleThread->stackPointer, false, idleProcedure, nullptr);
     auto* stack = static_cast<InterruptFrame*>(idleThread->stackPointer);
#ifdef ARCH_X8664
     stack->rsp -= 8;
#elifdef ARCH_ARM64
     stack->sp -= 8;
#endif

     std::atomic_ref(g_cpuLocalArray[cpuId]).store(cpuLocal, std::memory_order::release);

     KiSetCpuLocal(cpuLocal);

     g_cpuCount.fetch_add(1, std::memory_order::release);

     g_schedulerInitialised.store(true, std::memory_order::release);
     auto mask = 1uz << cpuId;
     g_schedulerGSBaseMask.fetch_or(mask);

     while (haltInProgress.exchange(true, std::memory_order::acq_rel)) operations::Yield();
     debugging::DbgWrite(
         u8"Initialised scheduler on CPU {}. kernel stack = {:x} => {:x}; idle stack = {:x} => {:x}\r\n", cpuId,
         stackPointer, stackPointer + process::ThreadStackSize, reinterpret_cast<std::uintptr_t>(idleThread->stackBase),
         reinterpret_cast<std::uintptr_t>(idleThread->stackBase) + process::ThreadStackSize);
     haltInProgress.store(false, std::memory_order::release);

     operations::EnableInterrupts();
     return kernelThread;
}

namespace
{
     constexpr std::size_t DefaultKernelStackSize = 16384;

     void ThreadDestructor(void* objectBody) noexcept
     {
          debugging::DbgWrite(u8"Thread destructor called for thread object at {}\r\n", objectBody);
          auto* thread = static_cast<process::Thread*>(objectBody);
          if (!thread) return;

          debugging::DbgWrite(u8"Releasing resources for thread with ID {}\r\n", thread->id);

          if (thread->stackBase)
          {
               g_kernelProcess->ReleaseVirtualMemory(thread->stackBase, DefaultKernelStackSize,
                                                     memory::AllocationFlags::Release);
               thread->stackBase = nullptr;
               thread->stackPointer = nullptr;
          }

          std::atomic_ref(thread->state)
              .store(process::ThreadState::DeletedWaitingForRelease, std::memory_order::release);

          debugging::DbgWrite(u8"Thread with ID {} marked as DeletedWaitingForRelease\r\n", thread->id);
     }
} // namespace

bool process::KeHasRunnableThreads() noexcept { return false; }

object::Handle process::KiCreateKernelThread(ThreadRoutine entryPoint, void* parameter, ThreadPriority priority)
{
     if (!entryPoint)
     {
          debugging::DbgWrite(u8"Cannot create kernel thread with null entry point\r\n");
          return object::kInvalidHandle;
     }

     auto attributes = object::ObjectAttributes{
         .name = "",
         .type = object::ObjectType::Thread,
         .bodySize = sizeof(Thread),
         .destructor = ThreadDestructor,
         .desiredAccess = object::AccessRights::All,
     };

     object::Handle handle = object::ObCreateObject(*KeCurrentProcess(), attributes);
     if (handle == object::kInvalidHandle)
     {
          debugging::DbgWrite(u8"Failed to create thread object\r\n");
          return object::kInvalidHandle;
     }

     auto* thread = object::ObGetBody<Thread>(*KeCurrentProcess(), handle, object::ObjectType::Thread);
     if (!thread)
     {
          debugging::DbgWrite(u8"Failed to get thread object body for handle {}\r\n", handle);
          object::ObCloseHandle(*KeCurrentProcess(), handle);
          return object::kInvalidHandle;
     }

     new (thread) Thread{};

     thread->stackBase = static_cast<std::uint8_t*>(
         g_kernelProcess->AllocateVirtualMemory(nullptr, process::ThreadStackSize,
                                                memory::AllocationFlags::Commit | memory::AllocationFlags::Reserve |
                                                    memory::AllocationFlags::ImmediatePhysical,
                                                memory::MemoryProtection::ReadWrite));
     KASANAllocateHeap(thread->stackBase, process::ThreadStackSize);
     std::memset(thread->stackBase, 0xcc, process::ThreadStackSize);

     if (!thread->stackBase)
     {
          debugging::DbgWrite(u8"Failed to allocate kernel stack for thread\r\n");
          object::ObCloseHandle(*KeCurrentProcess(), handle);
          return object::kInvalidHandle;
     }

     auto sp =
         reinterpret_cast<std::uintptr_t>(thread->stackBase) + process::ThreadStackSize - sizeof(InterruptFrame) - 4096;
     sp &= ~0xfuz;

     memory::paging::ProtectPage(
         g_kernelProcess->GetPageTableBase(), (sp + 0x1000 + sizeof(InterruptFrame)) & ~0xfff, false, false, false,
         [](std::size_t) -> void*
         {
              std::uintptr_t page = memory::physicalAllocator.AllocatePage(memory::PFNUse::PageTable);
              if (page == ~0uz) return nullptr;
              return reinterpret_cast<void*>(page + memory::virtualOffset);
         });
     memory::paging::InvalidatePage(sp);

     thread->stackPointer = reinterpret_cast<std::byte*>(sp);
     thread->id = KiAllocateThreadId();
     thread->priority = priority;
     thread->parentProcess = KeCurrentProcess();
     thread->next = nullptr;
     thread->argument = 0;

     const auto index = static_cast<std::size_t>(priority);
     thread->quantumCounter = ThreadPriorityQuantum[index];

     KiInitialiseThreadStacks(thread->stackPointer, false, reinterpret_cast<void*>(entryPoint), parameter);
     auto* stack = static_cast<InterruptFrame*>(thread->stackPointer);
#ifdef ARCH_X8664
     stack->rsp -= 8;
#elifdef ARCH_ARM64
     stack->sp -= 8;
#endif

     std::atomic_ref(thread->state).store(ThreadState::Runnable, std::memory_order::release);

     auto* cpu = KeCurrentCpu();
     AcquireSelectionLock(cpu);
     AddToReadyQueue(cpu, thread);
     ReleaseSelectionLock(cpu);

     while (haltInProgress.exchange(true, std::memory_order::acq_rel)) operations::Yield();
     debugging::DbgWrite(u8"Created kernel thread with ID {} P={} SP={}\r\n", thread->id, static_cast<int>(priority),
                         thread->stackPointer);
     haltInProgress.store(false, std::memory_order::release);

     return handle;
}

void process::KeSleepCurrentThread(std::uint64_t milliseconds)
{
     auto* currentThread = KeCurrentThread();
     if (!currentThread)
     {
          debugging::DbgWrite(u8"KeSleepCurrentThread called with no current thread\r\n");
          return;
     }

     if (milliseconds == 0)
     {
          debugging::DbgWrite(u8"Thread ID {} yielding CPU\r\n", currentThread->id);
          currentThread->quantumCounter = 0;
          return;
     }

     const std::uint64_t now = KeCurrentSystemTime();
     const std::uint64_t wakeupTimeMs = now + milliseconds;

     auto* entry = new SleepEntry{
         .thread = currentThread,
         .wakeupTimeMs = wakeupTimeMs,
         .next = nullptr,
     };

     AcquireSleepQueueLock();
     {
          if (!g_sleepQueue.head || g_sleepQueue.head->wakeupTimeMs > wakeupTimeMs)
          {
               entry->next = g_sleepQueue.head;
               g_sleepQueue.head = entry;
          }
          else
          {
               SleepEntry* previous = g_sleepQueue.head;
               while (previous->next && previous->next->wakeupTimeMs <= wakeupTimeMs) previous = previous->next;
               entry->next = previous->next;
               previous->next = entry;
          }
     }
     ReleaseSleepQueueLock();

     currentThread->argument = wakeupTimeMs;
     currentThread->quantumCounter = 0;

     std::atomic_ref(currentThread->state).store(ThreadState::Sleeping, std::memory_order::release);

     while (std::atomic_ref(currentThread->state).load(std::memory_order::acquire) == ThreadState::Sleeping)
          operations::Halt();
}

void process::KiPsWakeThread(Thread* thread)
{
     if (!thread) return;

     auto* cpu = KeCurrentCpu();

     const auto state = std::atomic_ref(thread->state).load(std::memory_order::acquire);

     if (state == ThreadState::Sleeping || state == ThreadState::EventWait || state == ThreadState::InterruptWait)
     {
          AcquireSelectionLock(cpu);
          std::atomic_ref(thread->state).store(ThreadState::Runnable, std::memory_order::release);
          AddToReadyQueue(cpu, thread);
          ReleaseSelectionLock(cpu);
     }
}

void process::KiPsYieldThread()
{
     auto* currentThread = KeCurrentThread();
     if (!currentThread) return;

     currentThread->quantumCounter = 0;
}

void process::KiPsBlockThread(ThreadState newState)
{
     auto* currentThread = KeCurrentThread();
     if (!currentThread) return;

     std::atomic_ref(currentThread->state).store(newState, std::memory_order::release);
     currentThread->quantumCounter = 0;
}

void process::KeExitCurrentThread()
{
     auto* currentThread = KeCurrentThread();
     if (!currentThread) __debugbreak();

     std::atomic_ref(currentThread->state).store(ThreadState::Finished, std::memory_order::release);
     currentThread->quantumCounter = 0;

     while (true) operations::Halt();
}
