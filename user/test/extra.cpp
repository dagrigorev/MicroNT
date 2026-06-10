// extra.cpp -- a tiny dependency-free DLL used to prove runtime LoadLibraryA +
// GetProcAddress: win32test does NOT import it; it loads it at runtime.
#include <stdint.h>

extern "C" {
__declspec(dllexport) uint32_t ExtraAddOne(uint32_t x) { return x + 1; }
__declspec(dllexport) uint32_t ExtraMagic() { return 0x1234; }
}
