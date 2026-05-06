; syscall_entry.asm - MicroNT SYSCALL dispatch + M15 exception delivery
;
; MULTI-THREAD SAFETY:
;   s_user_rsp is scratch only for the IF=0 window before user RSP is pushed
;   onto the per-thread kernel stack.  switch_context preserves it across
;   context switches.
;
; EXCEPTION DELIVERY (M15):
;   KiSystemCall sets g_pending_exception_va = handler_VA before returning.
;   After sysret restores user RSP/RIP/RFLAGS, we redirect RIP to the handler
;   and clear the global.  The exception code was already returned in RAX.
;   The handler's `ret` pops the original call-site return address and resumes
;   normal user execution.

BITS 64
default rel

section .text

extern  g_kernel_rsp0
extern  KiSystemCall
extern  g_pending_exception_va   ; u64: 0 = normal, else = handler VA to jump to

section .bss
align   8
s_user_rsp: resq 1      ; scratch - valid only while IF=0

section .text

global  syscall_entry
syscall_entry:
    ; Step 1: switch to kernel stack (IF=0 via SFMASK)
    mov     [s_user_rsp], rsp
    mov     rsp, [g_kernel_rsp0]

    ; Step 2: push {user_rsp, user_rip, user_rflags} onto per-thread kernel stack
    push    qword [s_user_rsp]      ; user RSP  - survives context switches
    push    rcx                     ; user RIP  (saved by SYSCALL)
    push    r11                     ; user RFLAGS

    ; Step 3: re-enable interrupts
    sti

    ; Step 4: rearrange args (SYSCALL -> SysV ABI)
    mov     r9,  r8
    mov     r8,  r10
    mov     rcx, rdx
    mov     rdx, rsi
    mov     rsi, rdi
    mov     rdi, rax

    ; Step 5: dispatch
    call    KiSystemCall            ; RAX = return value / exception code

    ; Step 6: return to user
    cli
    pop     r11                     ; user RFLAGS
    pop     rcx                     ; user RIP  (normal return address)
    pop     rsp                     ; user RSP  (per-thread, safe across sleeps)

    ; Step 7: M15 exception delivery - redirect sysret target if pending
    push    rax                     ; save syscall return value
    mov     rax, [g_pending_exception_va]
    test    rax, rax
    jz      .no_exception
    mov     qword [g_pending_exception_va], 0   ; clear (IF=0, single-CPU, safe)
    mov     rcx, rax               ; override user RIP -> jump to handler
.no_exception:
    pop     rax                     ; restore return value (= exception code if raised)

    o64 sysret                      ; RIP<-RCX  RFLAGS<-R11  CS/SS<-STAR
