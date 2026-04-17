#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include "../process/taskScheduler.h"
#include "utils/operations.h"

namespace synchronisation
{
     struct SpinLock
     {
          std::atomic<bool> isLocked{};

          void Lock()
          {
               std::size_t backoff = 1;

               while (true)
               {
                    while (isLocked.load(std::memory_order::relaxed))
                         for (std::size_t i = 0; i < backoff; i++) operations::Yield();

                    if (!isLocked.exchange(true, std::memory_order::acquire)) return;

                    backoff = std::min(backoff << 1uz, 64uz);
               }
          }

          void Unlock() { isLocked.store(false, std::memory_order::release); }
     };

     struct PushLock
     {
          static constexpr std::uint32_t ReaderIncrement = 1;
          static constexpr std::uint32_t ReaderMask = 0x3FFF'FFFF;
          static constexpr std::uint32_t WriterActive = 1u << 30;
          static constexpr std::uint32_t WriterPending = 1u << 31;

          void LockShared() noexcept
          {
               std::size_t spins = 0;

               while (true)
               {
                    std::uint32_t state = _state.load(std::memory_order::acquire);

                    if ((state & (WriterActive | WriterPending)) == 0)
                    {
                         if (_state.compare_exchange_weak(state, state + ReaderIncrement, std::memory_order::acquire,
                                                          std::memory_order::relaxed))
                              return;
                    }

                    SpinOrPark(spins);
               }
          }

          void UnlockShared() noexcept
          {
               if (_state.fetch_sub(ReaderIncrement, std::memory_order::release) == ReaderIncrement) {}
          }

          void LockExclusive() noexcept
          {
               std::size_t spins = 0;

               while (true)
               {
                    std::uint32_t state = _state.load(std::memory_order::relaxed);

                    if ((state & (ReaderMask | WriterActive)) == 0)
                    {
                         std::uint32_t desired = WriterActive;

                         if (_state.compare_exchange_weak(state, desired, std::memory_order::acquire,
                                                          std::memory_order::relaxed))
                              return;
                    }

                    if (!(state & WriterPending)) { _state.fetch_or(WriterPending, std::memory_order::relaxed); }

                    SpinOrPark(spins);
               }
          }

          void UnlockExclusive() noexcept { _state.store(0, std::memory_order::release); }

     private:
          std::atomic<std::uint32_t> _state{0};

          static void CpuRelax() noexcept { operations::Yield(); }

          static void SpinOrPark(std::size_t& spins)
          {
               if (spins < 250)
               {
                    CpuRelax();
                    ++spins;
                    return;
               }
               process::KeSleepCurrentThread(1);

               spins = 0;
          }
     };
} // namespace synchronisation
