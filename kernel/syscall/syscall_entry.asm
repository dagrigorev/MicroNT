; syscall_entry.asm - MicroNT SYSCALL dispatch stub
;
; MULTI-THREAD SAFETY:
;   s_user_rsp is a single-CPU scratch for the brief window between
;   "park user RSP" and "push it onto the kernel stack".  IF=0 during
;   this window so no other thread can preempt.  Once user RSP is pushed
;   onto the per-thread kernel stack, it survives context switches safely.

BITS 64
default rel

section .text

extern  g_kernel_rsp0
extern  KiSystemCall

section .bss
align   8
s_user_rsp: resq 1      ; scratch - safe while IF=0

section .text

global  syscall_entry
syscall_entry:
    ; Step 1: switch to kernel stack
    mov     [s_user_rsp], rsp           ; park user RSP (IF=0, atomic window)
    mov     rsp, [g_kernel_rsp0]        ; switch to per-thread kernel stack

    ; Step 2: push {user_rsp, user_rip, user_rflags} onto kernel stack
    ;         All three values now live on the per-thread kernel stack and
    ;         are saved/restored by switch_context across sleep/block calls.
    push    qword [s_user_rsp]          ; user RSP
    push    rcx                         ; user RIP  (saved by SYSCALL instruction)
    push    r11                         ; user RFLAGS

    ; Step 3: re-enable interrupts (safe: RSP is now on kernel stack)
    sti

    ; Step 4: rearrange args for SysV ABI
    ;   SYSCALL convention: RAX=number RDI=a1 RSI=a2 RDX=a3 R10=a4 R8=a5 R9=a6
    ;   SysV C:             RDI=number RSI=a1 RDX=a2 RCX=a3 R8=a4  R9=a5
    mov     r9,  r8
    mov     r8,  r10
    mov     rcx, rdx
    mov     rdx, rsi
    mov     rsi, rdi
    mov     rdi, rax

    ; Step 5: dispatch
    call    KiSystemCall            ; RAX = return value

    ; Step 6: return to user  (pop in reverse push order)
    cli
    pop     r11                     ; user RFLAGS
    pop     rcx                     ; user RIP
    pop     rsp                     ; user RSP  <- from per-thread kernel stack

    o64 sysret                      ; RIP<-RCX  RFLAGS<-R11  CS/SS<-STAR
