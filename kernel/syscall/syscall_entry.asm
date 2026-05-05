; syscall_entry.asm - MicroNT M6 SYSCALL dispatch stub
;
; Called by SYSCALL instruction (ring-3 -> ring-0).
; At entry:
;   RSP  = USER stack (SYSCALL does NOT switch RSP)
;   RCX  = user return RIP (saved by CPU)
;   R11  = user RFLAGS (saved by CPU)
;   RAX  = syscall number
;   RDI  = arg1, RSI = arg2, RDX = arg3
;   R10  = arg4 (R10 instead of RCX which was clobbered by SYSCALL)
;   R8   = arg5, R9 = arg6
;   IF   = 0 (SFMASK cleared it)
;
; C handler prototype (SysV ABI):
;   u64 KiSystemCall(u64 number, u64 a1, u64 a2, u64 a3, u64 a4, u64 a5)
;
; SYSRETQ restores: RIP<-RCX, RFLAGS<-R11, CS/SS from STAR, RSP unchanged.

BITS 64
default rel

section .text

extern  g_kernel_rsp0   ; u64 global: top of current thread's kernel stack
extern  KiSystemCall

; Per-CPU scratch: save user RSP before switching stacks.
; Single-CPU kernel - one slot is enough.
section .bss
align   8
s_user_rsp: resq 1

section .text

global  syscall_entry
syscall_entry:
    ; --- Step 1: switch to kernel stack ---
    mov     [s_user_rsp], rsp           ; park user RSP in scratch
    mov     rsp, [g_kernel_rsp0]        ; load kernel stack top (TSS.RSP0 cache)

    ; --- Step 2: push SYSRET context ---
    push    rcx                         ; user RIP   (restored by o64 sysret)
    push    r11                         ; user RFLAGS (restored by o64 sysret)

    ; --- Step 3: enable interrupts (safe now that RSP is kernel) ---
    sti

    ; --- Step 4: rearrange args for SysV C ABI ---
    ; Before: RAX=number  RDI=a1  RSI=a2  RDX=a3  R10=a4  R8=a5  R9=a6
    ; After:  RDI=number  RSI=a1  RDX=a2  RCX=a3  R8=a4   R9=a5
    mov     r9,  r8         ; a5 <- R8  (save before overwrite)
    mov     r8,  r10        ; a4 <- R10
    mov     rcx, rdx        ; a3 <- RDX
    mov     rdx, rsi        ; a2 <- RSI
    mov     rsi, rdi        ; a1 <- RDI
    mov     rdi, rax        ; number <- RAX

    ; --- Step 5: call C dispatcher ---
    call    KiSystemCall    ; return value in RAX (syscall status/result)

    ; --- Step 6: return to user ---
    cli                     ; disable IRQs for clean stack switch
    pop     r11             ; restore user RFLAGS
    pop     rcx             ; restore user RIP
    mov     rsp, [s_user_rsp]   ; restore user RSP

    o64 sysret                 ; RIP<-RCX  RFLAGS<-R11  CS/SS<-STAR  RSP unchanged
