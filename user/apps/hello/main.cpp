// hello/main.cpp - MicroNT test PE application
// Compiled with MSVC/Clang-cl for x86_64, targeting MicroNT kernel32.dll stubs.
// TODO(M8): This will run natively on MicroNT once the PE loader + Win32 layer is complete.

#include <windows.h>

int main() {
    const char* msg = "Hello from MicroNT!\r\n";
    DWORD written = 0;
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    WriteFile(out, msg, (DWORD)lstrlenA(msg), &written, nullptr);
    return 0;
}
