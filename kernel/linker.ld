/* MicroNT kernel linker script */
/* Kernel loaded at 1 MB physical by UEFI bootloader */
OUTPUT_FORMAT("elf64-x86-64")
OUTPUT_ARCH(i386:x86-64)
ENTRY(_kernel_start)

KERNEL_PHYS_BASE = 0x0000000000100000;  /* 1 MB */

SECTIONS
{
    . = KERNEL_PHYS_BASE;

    .text ALIGN(4096) : {
        *(.text .text.*)
    }

    .rodata ALIGN(4096) : {
        *(.rodata .rodata.*)
    }

    .data ALIGN(4096) : {
        *(.data .data.*)
    }

    .bss ALIGN(4096) : {
        *(COMMON)
        *(.bss .bss.*)
    }

    kernel_end = ALIGN(4096);

    /DISCARD/ : {
        *(.comment)
        *(.eh_frame)
        *(.note*)
    }
}
