// cpu.cpp - MicroNT CPU detection and feature reporting

#include "../../include/hal.h"
#include "../../include/debug.h"

namespace {

struct CpuidResult { u32 eax, ebx, ecx, edx; };

static CpuidResult Cpuid(u32 leaf, u32 subleaf = 0) {
    CpuidResult r;
    __asm__ volatile("cpuid"
        : "=a"(r.eax), "=b"(r.ebx), "=c"(r.ecx), "=d"(r.edx)
        : "a"(leaf), "c"(subleaf));
    return r;
}

} // namespace

namespace HAL {

void CpuDetect() {
    // Vendor string from leaf 0
    char vendor[13] = {};
    auto r0 = Cpuid(0);
    *reinterpret_cast<u32*>(vendor + 0) = r0.ebx;
    *reinterpret_cast<u32*>(vendor + 4) = r0.edx;
    *reinterpret_cast<u32*>(vendor + 8) = r0.ecx;

    // Brand string from leaves 0x80000002-4
    char brand[49] = {};
    for (int i = 0; i < 3; ++i) {
        auto r = Cpuid(0x80000002 + i);
        u32* p = reinterpret_cast<u32*>(brand + i * 16);
        p[0] = r.eax; p[1] = r.ebx; p[2] = r.ecx; p[3] = r.edx;
    }

    KDBG_INFO("CPU vendor: %s", vendor);
    KDBG_INFO("CPU brand:  %s", brand);
}

} // namespace HAL
