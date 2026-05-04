; boot.asm - MicroNT kernel entry stub (UEFI path)
;
; The UEFI bootloader (bootloader.cpp) already:
;   - Transitions to 64-bit long mode (firmware does this)
;   - Sets up identity-mapped page tables
;   - Loads the kernel ELF segments
;   - Calls ExitBootServices
;
; This stub's only job:
;   1. Switch to the kernel's own stack
;   2. Adapt the calling convention (UEFI/Windows ABI → SysV ABI)
;   3. Call kernel_main

bits 64

section .text
global _kernel_start
extern kernel_main

_kernel_start:
    ; ---- Stack setup ----
    ; UEFI stack may be in EfiBootServicesData (reclaimable).
    ; Switch to our own static kernel stack immediately.
    lea  rsp, [rel stack_top]
    and  rsp, ~0xF          ; ensure 16-byte alignment
    xor  rbp, rbp           ; clear frame pointer

    ; ---- Calling convention conversion ----
    ; Bootloader calls us with Windows/UEFI ABI:
    ;   RCX = MicroNTBootInfo*  (arg1)
    ; kernel_main uses SysV ABI:
    ;   RDI = arg1
    mov  rdi, rcx

    ; ---- Clear direction flag (required by ABI) ----
    cld

    call kernel_main

    ; kernel_main should never return — halt if it does
.halt:
    cli
    hlt
    jmp .halt


section .bss
align 16
stack:      resb 32768      ; 32 KB kernel boot stack
stack_top:
