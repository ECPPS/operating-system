#pragma once

#include <cstdint>

namespace cpu
{
     void Initialise(std::uintptr_t (*lpPages)[2]); // NOLINT
     void KeProtect(void (*protectionFunction)(std::uintptr_t address, bool isCode));
} // namespace cpu
