// virtual_memory.cpp - MicroNT VMM (M1: stub)

#include "../include/memory.h"
#include "../include/debug.h"

namespace VMM {

void Init() {
    // M1: Identity map from boot.asm is already in effect.
    // TODO(M3): Set up proper higher-half kernel mapping,
    //           page fault handler, MapPage/UnmapPage.
    KDBG_INFO("VMM: M1 stub — identity map from boot.asm active");
}

} // namespace VMM
