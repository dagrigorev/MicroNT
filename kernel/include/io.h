#pragma once
// io.h - MicroNT I/O Manager interface

#include "ntdef.h"
#include "ntstatus.h"

// ============================================================
// I/O Manager (stub for M1)
// ============================================================
namespace IO {

void Init();

// Console subsystem
namespace Console {
    void Init();
    void Write(const char* buf, usize len);
    void WriteChar(char c);
    usize Read(char* buf, usize max_len);
}

} // namespace IO

// ============================================================
// Syscall layer (stub for M1)
// ============================================================
namespace SYSCALL {
void Init();
void SetCommands(const char** cmds, u32 count);
void SetupTestPipe(const char* name, const u8* data, u32 len);
} // namespace SYSCALL

// ============================================================
// VFS (M10/M30)
// ============================================================
constexpr u32 WNODE_DATA_MAX = 2048;

struct MicroNTBootInfo;
namespace VFS {
void Init(const MicroNTBootInfo* bi);
void SaveNVRAM();   // persist writable nodes to UEFI NVRAM
void ForEach(void (*cb)(const char* name, u64 size));
void ForEachWritable(void (*cb)(const char* name, u32 size, bool is_dir));
i32  Open(const char* name);
void Close(i32 handle);
i64  Read(i32 handle, u64 offset, void* buf, u64 len);
i64  Size(i32 handle);
const void* GetData(const char* name, u64* size_out);
const u8*   FindFile(const char* name, usize* out_size);
// Writable node API
u32  WFindOrCreate(const char* name, bool is_dir = false);
bool MkDir(const char* name);
bool Delete(const char* name);
u32  WWrite(u32 idx, const u8* src, u32 len);
u32  WRead(u32 idx, u64 offset, u8* dst, u32 len);
u32  WSize(u32 idx);
bool WIsDir(u32 idx);
} // namespace VFS
