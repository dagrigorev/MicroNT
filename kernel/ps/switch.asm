; switch.asm - MicroNT M5 context switch and thread entry trampolines
; Kernel uses SysV AMD64 ABI (x86_64-unknown-none-elf):
;   RDI = arg1, RSI = arg2, RDX = arg3
;   Callee-saved: RBX, RBP, R12-R15
;   Caller-saved: RAX, RCX, RDX, RSI, RDI, R8-R11

BITS 64
section .text

; ============================================================
; switch_context(KThread* prev, KThread* next)
;   RDI = prev  (save RSP here, at offset 0)
;   RSI = next  (load RSP from here, at offset 0)
;
; Saves callee-saved registers + implicit return address on the
; current (prev) kernel stack, then swaps stacks.
;
; Stack frame layout after saving (top = low address):
;   [RSP+0 ] r15      <- KernelStackPtr points here
;   [RSP+8 ] r14
;   [RSP+16] r13
;   [RSP+24] r12
;   [RSP+32] rbx
;   [RSP+40] rbp
;   [RSP+48] return address (caller's RIP via `call switch_context`)
; ============================================================
global switch_context
switch_context:
    ; Save callee-saved registers on current stack
    push    rbp
    push    rbx
    push    r12
    push    r13
    push    r14
    push    r15

    ; Save current RSP to prev->KernelStackPtr (offset 0 in KThread)
    mov     [rdi], rsp

    ; Load next RSP from next->KernelStackPtr (offset 0 in KThread)
    mov     rsp, [rsi]

    ; Restore next thread's callee-saved registers
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     rbx
    pop     rbp

    ; Return to next thread's saved RIP
    ret


; ============================================================
; kernel_thread_entry
;
; Entry point for freshly created kernel threads.
; Called via switch_context ret when a new kernel thread runs
; for the first time.
;
; Stack at entry: 16-byte aligned (kstack_top, no return address).
; Calls KernelThreadEntry (C++), which runs the thread function
; then calls PS::TerminateCurrentThread().  Never returns.
; ============================================================
extern  KernelThreadEntry

global  kernel_thread_entry
kernel_thread_entry:
    sti                         ; interrupts were off during switch; enable now
    call    KernelThreadEntry   ; KernelThreadEntry() -> runs EntryFn(EntryArg) -> terminates
    ; should never reach here
    cli
.spin:
    hlt
    jmp     .spin


; ============================================================
; user_thread_entry
;
; Entry point for freshly created user threads.
; Called via switch_context ret.  An IRETQ frame is already on
; the stack (placed by CreateUserThread):
;   [RSP+0 ] RIP_user
;   [RSP+8 ] CS = 0x1B  (user code, DPL=3)
;   [RSP+16] RFLAGS = 0x202  (IF=1, reserved bit)
;   [RSP+24] RSP_user
;   [RSP+32] SS = 0x23  (user data, DPL=3)
; ============================================================
global  user_thread_entry
user_thread_entry:
    sti                         ; ensure interrupts enabled (iretq restores RFLAGS too)
    iretq
