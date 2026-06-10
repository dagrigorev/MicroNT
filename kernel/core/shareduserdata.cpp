// shareduserdata.cpp -- KUSER_SHARED_DATA implementation.
//
// Field offsets follow the documented x64 KUSER_SHARED_DATA layout so that
// real Windows binaries reading SharedUserData (0x7FFE0000) find what they
// expect. We populate the OS version, system root and the live timing fields.

#include "../include/shareduserdata.h"
#include "../include/memory.h"
#include "../include/hal.h"
#include "../include/debug.h"

namespace KUSER {

// Documented KUSER_SHARED_DATA field offsets (x64).
enum : u32 {
    OFF_TickCountMultiplier = 0x004,
    OFF_InterruptTime       = 0x008,  // KSYSTEM_TIME
    OFF_SystemTime          = 0x014,  // KSYSTEM_TIME
    OFF_NtSystemRoot        = 0x030,  // WCHAR[260]
    OFF_NtProductType       = 0x264,  // ULONG (1 = NtProductWinNt)
    OFF_ProductTypeIsValid  = 0x268,  // BOOLEAN
    OFF_NtMajorVersion      = 0x26C,  // ULONG
    OFF_NtMinorVersion      = 0x270,  // ULONG
    OFF_NtBuildNumber       = 0x274,  // ULONG (kernel build, our extension slot)
    OFF_TickCount           = 0x320,  // KSYSTEM_TIME
};

static volatile u8* s_page = nullptr;   // identity-mapped view of the page

static void W32(u32 off, u32 v) {
    *reinterpret_cast<volatile u32*>(s_page + off) = v;
}

// Write a 64-bit value into a KSYSTEM_TIME (LowPart, High1Time, High2Time)
// using the documented lock-free update order.
static void WriteSystemTime(u32 off, u64 v) {
    volatile u32* low   = reinterpret_cast<volatile u32*>(s_page + off + 0);
    volatile i32* high1 = reinterpret_cast<volatile i32*>(s_page + off + 4);
    volatile i32* high2 = reinterpret_cast<volatile i32*>(s_page + off + 8);
    *high2 = (i32)(v >> 32);
    *low   = (u32)(v & 0xFFFFFFFFu);
    *high1 = (i32)(v >> 32);
}

void Init() {
    u64 phys = PMM::AllocPage();
    if (!phys) { KDBG_ERROR("KUSER: page alloc failed"); return; }
    s_page = reinterpret_cast<volatile u8*>(phys);   // identity-mapped (< RAM)
    for (u32 i = 0; i < PAGE_SIZE; ++i) s_page[i] = 0;

    // Static identity fields.
    W32(OFF_TickCountMultiplier, 0x0A000000u);   // GetTickCount = low*mult>>24 = ticks*10ms
    W32(OFF_NtProductType, 1u);                  // NtProductWinNt
    W32(OFF_ProductTypeIsValid, 1u);
    W32(OFF_NtMajorVersion, WIN_MAJOR);
    W32(OFF_NtMinorVersion, WIN_MINOR);
    W32(OFF_NtBuildNumber, WIN_BUILD);

    // NtSystemRoot = L"C:\\Windows"
    const char* root = "C:\\Windows";
    volatile u16* w = reinterpret_cast<volatile u16*>(s_page + OFF_NtSystemRoot);
    for (u32 i = 0; root[i]; ++i) w[i] = (u16)(u8)root[i];

    Tick();

    // Map it at the canonical 0x7FFE0000 in every (shared) address space.
    if (!VMM::MapSharedUserData(phys)) {
        KDBG_ERROR("KUSER: failed to map at 0x7FFE0000");
        return;
    }

    // Prove the mapping resolves in the active address space.
    volatile u32* v = reinterpret_cast<volatile u32*>(SHARED_USER_DATA_VA + OFF_NtMajorVersion);
    KDBG_INFO("KUSER: SharedUserData live -> reports Windows %u.%u build %u",
              v[0], *reinterpret_cast<volatile u32*>(SHARED_USER_DATA_VA + OFF_NtMinorVersion),
              *reinterpret_cast<volatile u32*>(SHARED_USER_DATA_VA + OFF_NtBuildNumber));
}

void Tick() {
    if (!s_page) return;
    u64 ticks = HAL::PitTicks();
    u64 time100ns = ticks * 100000ULL;   // 1 tick = 10 ms = 100000 * 100 ns
    WriteSystemTime(OFF_InterruptTime, time100ns);
    WriteSystemTime(OFF_SystemTime,    time100ns);
    WriteSystemTime(OFF_TickCount,     ticks);
}

u32 MajorVersion() { return WIN_MAJOR; }
u32 MinorVersion() { return WIN_MINOR; }
u32 BuildNumber()  { return WIN_BUILD; }

} // namespace KUSER
