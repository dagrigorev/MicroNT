; kernel_entry.asm - MicroNT kernel entry point
;
; Called by the UEFI bootloader after ExitBootServices.
; CPU is already in 64-bit long mode with UEFI's identity-map paging active.
; RDI = BootInfo*  (bootloader calls us with SysV ABI via sysv_abi attribute)

bits 64

section .text
global kernel_entry
extern kernel_main

kernel_entry:
    ; Install our own stack immediately.
    ; UEFI may have given us a small stack at an inconvenient location.
    lea  rsp, [rel _kernel_stack_top]

    ; Align stack to 16 bytes as required by SysV ABI before the call.
    and  rsp, ~0xFULL

    ; RDI already holds BootInfo* — pass it straight through to kernel_main.
    call kernel_main

    ; kernel_main should never return, but halt if it does.
.halt:
    cli
    hlt
    jmp  .halt

section .bss
align 16
_kernel_stack:     resb 32768   ; 32 KB kernel bootstrap stack
_kernel_stack_top:
