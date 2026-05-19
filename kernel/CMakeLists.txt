cmake_minimum_required(VERSION 3.20)

project(MicroNT_Kernel
    LANGUAGES C CXX ASM_NASM
    VERSION 0.1.0
)

# ============================================================
# Compiler / linker settings
# ============================================================
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Shared warning flags (C/CXX only - NASM doesn't understand these)
add_compile_options(
    $<$<NOT:$<COMPILE_LANGUAGE:ASM_NASM>>:-Wall>
    $<$<NOT:$<COMPILE_LANGUAGE:ASM_NASM>>:-Wextra>
    $<$<NOT:$<COMPILE_LANGUAGE:ASM_NASM>>:-Wpedantic>
    $<$<NOT:$<COMPILE_LANGUAGE:ASM_NASM>>:-Wno-unused-parameter>
    $<$<NOT:$<COMPILE_LANGUAGE:ASM_NASM>>:-Wno-gnu-zero-variadic-macro-arguments>
    $<$<NOT:$<COMPILE_LANGUAGE:ASM_NASM>>:-Wno-gnu-anonymous-struct>
)

# NASM flags
set(CMAKE_ASM_NASM_FLAGS "-f elf64")
set(CMAKE_ASM_NASM_OBJECT_FORMAT elf64)

# ============================================================
# Kernel sources
# ============================================================
set(KERNEL_SOURCES
    # Kernel entry stub (called by UEFI bootloader)
    ../boot/boot.asm

    # HAL
    hal/x64/serial.cpp
    hal/x64/cpu.cpp
    hal/x64/gdt.cpp
    hal/x64/pic.cpp
    hal/x64/idt.cpp
    hal/x64/irq.cpp
    hal/x64/pit.cpp
    hal/x64/vga.cpp
    hal/x64/keyboard.cpp
    hal/x64/interrupts.asm

    # Core
    core/debug.cpp
    core/panic.cpp
    core/kernel_main.cpp

    # Memory
    mm/physical_memory.cpp
    mm/virtual_memory.cpp
    mm/heap.cpp

    # Object manager
    ob/object_manager.cpp

    # Process manager
    ps/process.cpp
    ps/scheduler.cpp
    ps/switch.asm

    # Session manager / interactive shell bootstrap
    sm/appmodel.cpp
    sm/csrss.cpp
    sm/explorer.cpp
    sm/inputhost.cpp
    sm/profile.cpp
    sm/registry.cpp
    sm/services.cpp
    sm/shellhost.cpp
    sm/session_manager.cpp
    sm/userinit.cpp
    sm/winlogon.cpp

    # Win32 graphical subsystem boundary
    win32k/compositor.cpp
    win32k/desktopmodel.cpp
    win32k/displaycfg.cpp
    win32k/dwm.cpp
    win32k/shellcommands.cpp
    win32k/shellact.cpp
    win32k/shella11y.cpp
    win32k/shellinput.cpp
    win32k/shellnotify.cpp
    win32k/shellplaces.cpp
    win32k/shellpower.cpp
    win32k/shellstart.cpp
    win32k/shelltaskbar.cpp
    win32k/shelltray.cpp
    win32k/uxtheme.cpp
    win32k/windowmgr.cpp
    win32k/winsta.cpp

    # I/O
    io/console.cpp
    io/vfs.cpp

    # PE loader
    ldr/pe_loader.cpp

    # RTL
    rtl/string.cpp

    # Syscall layer
    syscall/syscall.cpp
    syscall/syscall_entry.asm
    sync/event.cpp
)

add_executable(micront.elf ${KERNEL_SOURCES})

target_include_directories(micront.elf PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Linker script
set(LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/linker.ld)
target_link_options(micront.elf PRIVATE
    -T ${LINKER_SCRIPT}
    # -nostdlib / -nostartfiles are clang driver flags; ld.lld is invoked
    # directly (CMAKE_CXX_LINK_EXECUTABLE) so these must not be passed.
)

set_target_properties(micront.elf PROPERTIES
    LINK_DEPENDS ${LINKER_SCRIPT}
    SUFFIX ""
)

# ============================================================
# Copy artifact to build root
# ============================================================
add_custom_command(TARGET micront.elf POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/../artifacts
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:micront.elf>
        ${CMAKE_BINARY_DIR}/../artifacts/micront.elf
    COMMENT "Copying kernel ELF to artifacts/"
)
