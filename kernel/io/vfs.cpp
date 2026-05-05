// vfs.cpp - MicroNT M10 Virtual File System
//
// Backed by the BootFile array in MicroNTBootInfo (files loaded by the
// bootloader from /boot/*.exe before ExitBootServices).  No disk driver
// is required; all data is in memory via the identity map.

#include "../include/bootinfo.h"
#include "../include/debug.h"
#include "../include/hal.h"
#include "../include/ntstatus.h"
#include "../include/spinlock.h"

// ============================================================
// File handle table (kernel-side)
// ============================================================
constexpr usize VFS_MAX_HANDLES = 32;

struct VfsHandle {
    bool     open;
    u64      phys_base;   // identity-mapped: phys == virt
    u64      size;
    u64      offset;      // current read position
    char     name[64];
};

namespace {

static VfsHandle   s_handles[VFS_MAX_HANDLES];
static Spinlock    s_lock;

// BootInfo pointer - set during Init
static const MicroNTBootInfo* s_bi = nullptr;

static void str_copy_n(char* dst, const char* src, usize max) {
    usize i=0; while (i+1<max && src[i]) { dst[i]=src[i]; i++; } dst[i]='\0';
}
[[maybe_unused]] static bool str_eq(const char* a, const char* b) {
    while (*a && *a==*b) { a++; b++; }
    return *a==*b;
}
// Case-insensitive compare (ASCII)
static bool str_ieq(const char* a, const char* b) {
    while (*a && *b) {
        char ca=*a, cb=*b;
        if (ca>='A'&&ca<='Z') ca+=32;
        if (cb>='A'&&cb<='Z') cb+=32;
        if (ca!=cb) return false;
        a++; b++;
    }
    return *a==*b;
}

} // namespace

namespace VFS {

void Init(const MicroNTBootInfo* bi) {
    s_bi = bi;
    for (usize i=0;i<VFS_MAX_HANDLES;++i) s_handles[i].open=false;
    KDBG_INFO("VFS: initialized with %u boot file(s)", bi ? bi->boot_file_count : 0);
    if (bi) {
        for (u32 i=0;i<bi->boot_file_count;++i)
            KDBG_INFO("VFS:   [%u] '%s' %llu bytes at phys 0x%llx",
                      i, bi->boot_files[i].name,
                      bi->boot_files[i].size,
                      bi->boot_files[i].phys_base);
    }
}

// List all files. Calls cb(name, size) for each.
void ForEach(void (*cb)(const char* name, u64 size)) {
    if (!s_bi) return;
    for (u32 i=0;i<s_bi->boot_file_count;++i)
        cb(s_bi->boot_files[i].name, s_bi->boot_files[i].size);
}

// Open a file by name. Returns kernel handle index (0-based) or -1 on error.
i32 Open(const char* name) {
    if (!s_bi || !name) return -1;
    // Find file in BootInfo
    const BootFile* found = nullptr;
    for (u32 i=0;i<s_bi->boot_file_count;++i) {
        if (str_ieq(s_bi->boot_files[i].name, name)) {
            found = &s_bi->boot_files[i];
            break;
        }
    }
    if (!found) {
        KDBG_WARN("VFS: file not found: '%s'", name);
        return -1;
    }
    // Allocate handle
    SpinlockGuard g(s_lock);
    for (usize i=0;i<VFS_MAX_HANDLES;++i) {
        if (!s_handles[i].open) {
            s_handles[i].open      = true;
            s_handles[i].phys_base = found->phys_base;
            s_handles[i].size      = found->size;
            s_handles[i].offset    = 0;
            str_copy_n(s_handles[i].name, found->name, 64);
            KDBG_TRACE("VFS: opened '%s' -> handle %llu", name, (u64)i);
            return (i32)i;
        }
    }
    KDBG_ERROR("VFS: handle table full");
    return -1;
}

void Close(i32 handle) {
    if (handle < 0 || (usize)handle >= VFS_MAX_HANDLES) return;
    SpinlockGuard g(s_lock);
    s_handles[handle].open = false;
}

// Read up to 'len' bytes at 'offset' from file 'handle'.
// Returns bytes read, or negative on error.
i64 Read(i32 handle, u64 offset, void* buf, u64 len) {
    if (handle < 0 || (usize)handle >= VFS_MAX_HANDLES) return -1;
    VfsHandle& h = s_handles[handle];
    if (!h.open) return -1;
    if (offset >= h.size) return 0;
    u64 avail = h.size - offset;
    if (len > avail) len = avail;
    if (len == 0) return 0;
    // Identity map: phys == virt for < 4GB
    const u8* src = reinterpret_cast<const u8*>(h.phys_base + offset);
    u8* dst = static_cast<u8*>(buf);
    for (u64 i=0;i<len;++i) dst[i] = src[i];
    return (i64)len;
}

i64 Size(i32 handle) {
    if (handle < 0 || (usize)handle >= VFS_MAX_HANDLES) return -1;
    if (!s_handles[handle].open) return -1;
    return (i64)s_handles[handle].size;
}

// Look up file data pointer directly (for PE loading)
const void* GetData(const char* name, u64* size_out) {
    if (!s_bi || !name) return nullptr;
    for (u32 i=0;i<s_bi->boot_file_count;++i) {
        if (str_ieq(s_bi->boot_files[i].name, name)) {
            if (size_out) *size_out = s_bi->boot_files[i].size;
            return reinterpret_cast<const void*>(s_bi->boot_files[i].phys_base);
        }
    }
    return nullptr;
}

} // namespace VFS
