#pragma once

#include <cstdint>


std::uint64_t KeDetectAndInitialiseProcessors(std::uintptr_t address);
std::uint64_t KeCPUCount();
