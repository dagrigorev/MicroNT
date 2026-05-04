# Toolchain-kernel.cmake
# Cross-compile for x86_64 freestanding ELF kernel using LLVM/Clang + ld.lld

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# Use clang/clang++ from PATH (or override via LLVM_PATH env var)
if(DEFINED ENV{LLVM_PATH})
    set(LLVM_BIN "$ENV{LLVM_PATH}/bin/")
else()
    set(LLVM_BIN "")
endif()

set(CMAKE_C_COMPILER        "${LLVM_BIN}clang"   CACHE STRING "" FORCE)
set(CMAKE_CXX_COMPILER      "${LLVM_BIN}clang++" CACHE STRING "" FORCE)
set(CMAKE_ASM_NASM_COMPILER "nasm"               CACHE STRING "" FORCE)
set(CMAKE_LINKER            "${LLVM_BIN}ld.lld"  CACHE STRING "" FORCE)

# ============================================================
# Invoke ld.lld directly instead of going through the clang++ driver.
# On Windows the driver tries to locate 'gcc' as back-end linker and
# fails when it isn't installed.
# <LINK_FLAGS> carries the -T <linker_script> from CMakeLists.txt.
# ============================================================
set(CMAKE_CXX_LINK_EXECUTABLE
    "<CMAKE_LINKER> <LINK_FLAGS> <OBJECTS> -o <TARGET> <LINK_LIBRARIES>"
    CACHE STRING "" FORCE)

# Target triple: bare-metal x86_64
set(TARGET_TRIPLE "x86_64-unknown-none-elf")

set(KERNEL_FLAGS
    "--target=${TARGET_TRIPLE}"
    "-ffreestanding"
    "-nostdlib"
    "-nostdinc"
    "-fno-exceptions"
    "-fno-rtti"
    "-fno-stack-protector"
    "-mno-red-zone"
    "-mno-mmx"
    "-mno-sse"
    "-mno-sse2"
    "-mcmodel=kernel"
    "-m64"
)

string(JOIN " " KERNEL_FLAGS_STR ${KERNEL_FLAGS})

set(CMAKE_C_FLAGS_INIT   "${KERNEL_FLAGS_STR} -std=c11")
set(CMAKE_CXX_FLAGS_INIT "${KERNEL_FLAGS_STR} -std=c++20")

# No driver-level linker flags — ld.lld is invoked directly.
set(CMAKE_EXE_LINKER_FLAGS_INIT "")

# Prevent CMake from testing the compiler with a full link
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
