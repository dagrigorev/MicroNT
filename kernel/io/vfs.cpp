// vfs.cpp - MicroNT M30 Virtual File System
//
// Two-tier storage:
//   Tier 1 (read-only): boot files loaded by the bootloader from /boot/*.
//   Tier 2 (writable):  16-node in-memory table; persists across reboots via
//                       UEFI NVRAM variable "MicroNTVFS" (SetVariable/GetVariable).

#include "../include/bootinfo.h"
#include "../include/debug.h"
#include "../include/hal.h"
#include "../include/ntstatus.h"
#include "../include/spinlock.h"

// ============================================================
// Writable node table
// ============================================================
constexpr u32 WNODE_MAX      = 16;
constexpr u32 WNODE_NAME_MAX = 64;
constexpr u32 WNODE_DATA_MAX = 2048;

struct WNode {
    char   name[WNODE_NAME_MAX];
    bool   used;
    bool   is_dir;
    u32    size;
    u8     data[WNODE_DATA_MAX];
};

// NVRAM persistence format (stored as one UEFI variable)
struct NVRAMHeader {
    u32 magic;      // 'NTFS' = 0x5346544E
    u32 count;
    WNode nodes[WNODE_MAX];
};
static constexpr u32 NVRAM_MAGIC = 0x5346544Eu;  // "NTFS"

// UEFI runtime services function pointers (ms_abi after ExitBootServices)
struct EFI_GUID { u32 d1; u16 d2, d3; u8 d4[8]; };
typedef u64 (__attribute__((ms_abi)) *EFI_GET_VAR)(
    const u16*, const EFI_GUID*, u32*, u64*, void*);
typedef u64 (__attribute__((ms_abi)) *EFI_SET_VAR)(
    const u16*, const EFI_GUID*, u32, u64, const void*);

// Minimal EFI_RUNTIME_SERVICES layout (only fields we need)
struct EFI_RT {
    u8   _hdr[24];          // EFI_TABLE_HEADER
    void* _time[4];         // GetTime, SetTime, GetWakeupTime, SetWakeupTime
    void* _virt[2];         // SetVirtualAddressMap, ConvertPointer
    void* GetVariable;      // offset 72
    void* GetNextVarName;   // offset 80
    void* SetVariable;      // offset 88
};

// Custom GUID for our VFS variable
// {B3F21A42-9C1D-4E7A-8B0F-2C5D6A7E9301}
static const EFI_GUID s_vfs_guid = {
    0xB3F21A42, 0x9C1D, 0x4E7A,
    { 0x8B, 0x0F, 0x2C, 0x5D, 0x6A, 0x7E, 0x93, 0x01 }
};

// NVRAM variable name (UTF-16)
static const u16 s_vfs_varname[] = {
    'M','i','c','r','o','N','T','V','F','S', 0
};

namespace {

static WNode   s_wnodes[WNODE_MAX];
static Spinlock s_wlock;

static const MicroNTBootInfo* s_bi        = nullptr;
static u64                    s_rt_va     = 0;  // EFI_RUNTIME_SERVICES*

// ---- string helpers -------------------------------------------------
static bool str_ieq(const char* a, const char* b) {
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca>='A'&&ca<='Z') ca+=32;
        if (cb>='A'&&cb<='Z') cb+=32;
        if (ca!=cb) return false;
        a++; b++;
    }
    return *a == *b;
}
static void str_copy(char* d, const char* s, u32 max) {
    u32 i=0; while (i+1<max && s[i]) { d[i]=s[i]; i++; } d[i]='\0';
}

// ---- NVRAM load/save ------------------------------------------------
static void LoadNVRAM() {
    if (!s_rt_va) return;
    EFI_RT* rt = reinterpret_cast<EFI_RT*>(s_rt_va);
    EFI_GET_VAR getVar = reinterpret_cast<EFI_GET_VAR>(rt->GetVariable);
    if (!getVar) return;

    NVRAMHeader hdr{};
    u64 size = sizeof(hdr);
    u32 attrs = 0;
    u64 status = getVar(s_vfs_varname, &s_vfs_guid, &attrs, &size, &hdr);
    if (status != 0 || hdr.magic != NVRAM_MAGIC) {
        KDBG_INFO("VFS: no NVRAM data found (first boot or cleared)");
        return;
    }
    u32 count = hdr.count < WNODE_MAX ? hdr.count : WNODE_MAX;
    for (u32 i = 0; i < count; ++i) s_wnodes[i] = hdr.nodes[i];
    KDBG_INFO("VFS: loaded %u writable node(s) from NVRAM", count);
}

} // namespace

namespace VFS {

void Init(const MicroNTBootInfo* bi) {
    s_bi    = bi;
    s_rt_va = bi ? bi->efi_runtime_va : 0;
    for (u32 i = 0; i < WNODE_MAX; ++i) {
        s_wnodes[i] = {};
    }
    if (bi) {
        KDBG_INFO("VFS: %u boot file(s)", bi->boot_file_count);
        for (u32 i = 0; i < bi->boot_file_count; ++i)
            KDBG_INFO("VFS:  [%u] '%s' %llu bytes", i,
                bi->boot_files[i].name, bi->boot_files[i].size);
    }
    // Restore writable nodes from UEFI NVRAM (survives across reboots)
    LoadNVRAM();
}

// Save writable node table to UEFI NVRAM variable.
// Call this before shutdown so data survives reboot.
void SaveNVRAM() {
    if (!s_rt_va) { KDBG_WARN("VFS: no runtime services, cannot persist"); return; }
    EFI_RT* rt = reinterpret_cast<EFI_RT*>(s_rt_va);
    EFI_SET_VAR setVar = reinterpret_cast<EFI_SET_VAR>(rt->SetVariable);
    if (!setVar) return;

    NVRAMHeader hdr{};
    hdr.magic = NVRAM_MAGIC;
    hdr.count = WNODE_MAX;
    for (u32 i = 0; i < WNODE_MAX; ++i) hdr.nodes[i] = s_wnodes[i];

    constexpr u32 ATTRS = 0x07; // NON_VOLATILE | BOOTSERVICE_ACCESS | RUNTIME_ACCESS
    u64 status = setVar(s_vfs_varname, &s_vfs_guid, ATTRS, sizeof(hdr), &hdr);
    if (status == 0) KDBG_INFO("VFS: NVRAM saved (%u nodes)", WNODE_MAX);
    else             KDBG_WARN("VFS: NVRAM save failed (status 0x%llx)", status);
}

// Iterate over boot (read-only) files.
void ForEach(void (*cb)(const char* name, u64 size)) {
    if (!s_bi) return;
    for (u32 i = 0; i < s_bi->boot_file_count; ++i)
        cb(s_bi->boot_files[i].name, s_bi->boot_files[i].size);
}

// Iterate over writable nodes.
void ForEachWritable(void (*cb)(const char* name, u32 size, bool is_dir)) {
    SpinlockGuard g(s_wlock);
    for (u32 i = 0; i < WNODE_MAX; ++i)
        if (s_wnodes[i].used)
            cb(s_wnodes[i].name, s_wnodes[i].size, s_wnodes[i].is_dir);
}

// Open a read-only boot file. Returns handle index (0-based) or -1.
i32 Open(const char* name) {
    // (handled by syscall.cpp VFS handle table -- just look up by name)
    if (!s_bi || !name) return -1;
    for (u32 i = 0; i < s_bi->boot_file_count; ++i)
        if (str_ieq(s_bi->boot_files[i].name, name))
            return (i32)i;
    return -1;
}

void Close(i32 /*handle*/) {}  // boot file handles are stateless

i64 Read(i32 handle, u64 offset, void* buf, u64 len) {
    if (!s_bi || handle < 0 || (u32)handle >= s_bi->boot_file_count) return -1;
    const BootFile& f = s_bi->boot_files[handle];
    if (offset >= f.size) return 0;
    u64 avail = f.size - offset;
    if (len > avail) len = avail;
    const u8* src = reinterpret_cast<const u8*>(f.phys_base + offset);
    for (u64 i = 0; i < len; ++i) static_cast<u8*>(buf)[i] = src[i];
    return (i64)len;
}

i64 Size(i32 handle) {
    if (!s_bi || handle < 0 || (u32)handle >= s_bi->boot_file_count) return -1;
    return (i64)s_bi->boot_files[handle].size;
}

const void* GetData(const char* name, u64* size_out) {
    if (!s_bi || !name) return nullptr;
    for (u32 i = 0; i < s_bi->boot_file_count; ++i)
        if (str_ieq(s_bi->boot_files[i].name, name)) {
            if (size_out) *size_out = s_bi->boot_files[i].size;
            return reinterpret_cast<const void*>(s_bi->boot_files[i].phys_base);
        }
    return nullptr;
}

const u8* FindFile(const char* name, usize* out_size) {
    u64 sz = 0;
    const void* p = GetData(name, &sz);
    if (out_size) *out_size = (usize)sz;
    return static_cast<const u8*>(p);
}

// ---- Writable node API ----------------------------------------------

// Find writable node by name. Returns index or WNODE_MAX if not found.
u32 WFind(const char* name) {
    for (u32 i = 0; i < WNODE_MAX; ++i)
        if (s_wnodes[i].used && str_ieq(s_wnodes[i].name, name))
            return i;
    return WNODE_MAX;
}

// Find or allocate a writable file node.
u32 WFindOrCreate(const char* name, bool is_dir = false) {
    SpinlockGuard g(s_wlock);
    for (u32 i = 0; i < WNODE_MAX; ++i)
        if (s_wnodes[i].used && str_ieq(s_wnodes[i].name, name))
            return i;
    for (u32 i = 0; i < WNODE_MAX; ++i)
        if (!s_wnodes[i].used) {
            s_wnodes[i] = {};
            s_wnodes[i].used   = true;
            s_wnodes[i].is_dir = is_dir;
            str_copy(s_wnodes[i].name, name, WNODE_NAME_MAX);
            return i;
        }
    return WNODE_MAX;  // table full
}

// Create a directory entry. Returns true on success.
bool MkDir(const char* name) {
    if (!name || !name[0]) return false;
    if (WFind(name) < WNODE_MAX) return false;  // already exists
    return WFindOrCreate(name, true) < WNODE_MAX;
}

// Delete a file or directory by name. Returns true if found and removed.
bool Delete(const char* name) {
    SpinlockGuard g(s_wlock);
    for (u32 i = 0; i < WNODE_MAX; ++i)
        if (s_wnodes[i].used && str_ieq(s_wnodes[i].name, name)) {
            s_wnodes[i] = {};
            return true;
        }
    return false;
}

// Write data to a writable file node.
u32 WWrite(u32 idx, const u8* src, u32 len) {
    if (idx >= WNODE_MAX || !s_wnodes[idx].used) return 0;
    WNode& n = s_wnodes[idx];
    u32 space = WNODE_DATA_MAX - n.size;
    if (len > space) len = space;
    for (u32 i = 0; i < len; ++i) n.data[n.size++] = src[i];
    return len;
}

// Read data from a writable file node.
u32 WRead(u32 idx, u64 offset, u8* dst, u32 len) {
    if (idx >= WNODE_MAX || !s_wnodes[idx].used) return 0;
    WNode& n = s_wnodes[idx];
    if (offset >= n.size) return 0;
    u32 avail = n.size - (u32)offset;
    if (len > avail) len = avail;
    for (u32 i = 0; i < len; ++i) dst[i] = n.data[(u32)offset + i];
    return len;
}

u32 WSize(u32 idx) {
    if (idx >= WNODE_MAX || !s_wnodes[idx].used) return 0;
    return s_wnodes[idx].size;
}

bool WIsDir(u32 idx) {
    if (idx >= WNODE_MAX || !s_wnodes[idx].used) return false;
    return s_wnodes[idx].is_dir;
}

} // namespace VFS
