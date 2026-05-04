# MicroNT Compatibility Matrix

## Win32 API Status (as of M1)

| API | Status | Notes |
|-----|--------|-------|
| ExitProcess | 🔲 M8 | |
| GetStdHandle | 🔲 M8 | STDIN/STDOUT/STDERR |
| WriteFile | 🔲 M8 | Console output only initially |
| ReadFile | 🔲 M8 | Console input only initially |
| GetLastError | 🔲 M8 | TEB-based |
| SetLastError | 🔲 M8 | |
| VirtualAlloc | 🔲 M8 | Backed by VMM::MapPage |
| VirtualFree | 🔲 M8 | |
| CloseHandle | 🔲 M8 | |
| GetModuleHandleA/W | 🔲 M8 | |
| GetProcAddress | 🔲 M8 | Export table scan |
| CreateFileA/W | 🔲 M9+ | VFS needed |
| MessageBoxA | 🔲 stub | Returns IDOK only |

## NT Native API Status

| API | Status | Notes |
|-----|--------|-------|
| NtTerminateProcess | 🔲 M6 | |
| NtWriteFile | 🔲 M6 | Console |
| NtReadFile | 🔲 M6 | Console |
| NtCreateFile | 🔲 M9 | |
| NtClose | 🔲 M6 | |
| NtAllocateVirtualMemory | 🔲 M6 | |
| NtFreeVirtualMemory | 🔲 M6 | |
| NtCreateEvent | 🔲 M6 | |
| NtWaitForSingleObject | 🔲 M6 | |

## Supported PE Features

| Feature | Status |
|---------|--------|
| PE32+ (x64) | ✅ M1 (validation) |
| Base relocations | ✅ M1 |
| Import by name | 🔲 M7 |
| Import by ordinal | 🔲 M7 stub |
| Export table | 🔲 M7 |
| TLS | ❌ Not planned short-term |
| SEH / unwind | ❌ Not planned short-term |
| Delay imports | ❌ Not planned |
| .NET / CLR | ❌ Out of scope |

## Test Programs (Planned)

| Program | Description | Target Milestone |
|---------|-------------|-----------------|
| hello.exe | Prints "Hello from MicroNT!" via WriteFile | M8 |
| console-test.exe | Tests STDIN, STDOUT, error codes | M8 |
| memtest.exe | VirtualAlloc/Free stress test | M8 |
| shell.exe | Interactive command shell | M9 |
