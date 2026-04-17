#pragma once

#include <cstdint>
#include "process/process.h"
#include "utils/arch.h"

extern std::uint64_t g_bootCycles;
extern arch::LoaderParameterBlock* g_loaderBlock;
extern kernel::ProcessControlBlock* g_kernelProcess;
extern std::uintptr_t g_stackBase;
extern std::uintptr_t g_stackSize;
extern std::uintptr_t g_imageBase;
extern std::uintptr_t g_imageSize;

void KeInitialiseCpu(std::uintptr_t acpiPhysical);
void KeRemoveLeftoverMappings();
void KiIdleLoop();
