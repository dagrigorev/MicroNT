#pragma once
// shareduserdata.h -- KUSER_SHARED_DATA, the read-only page Windows maps at the
// fixed user address 0x7FFE0000 in every process. ntdll and many Win32 binaries
// read the OS version and timing fields from here directly, so providing it is a
// foundational piece of Windows kernel compatibility.

#include "ntdef.h"

namespace KUSER {

// Canonical address (SharedUserData) -- identical in every process.
constexpr u64 SHARED_USER_DATA_VA = 0x7FFE0000ULL;

// Reported OS identity (Windows 11 24H2).
constexpr u32 WIN_MAJOR = 10;
constexpr u32 WIN_MINOR = 0;
constexpr u32 WIN_BUILD = 26100;

// Allocate, populate and map the page at 0x7FFE0000. Call once after VMM::Init.
void Init();

// Refresh the volatile time fields (InterruptTime / SystemTime / TickCount).
// Call from the timer tick.
void Tick();

u32 MajorVersion();
u32 MinorVersion();
u32 BuildNumber();

} // namespace KUSER
