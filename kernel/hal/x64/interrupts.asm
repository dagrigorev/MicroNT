; interrupts.asm - MicroNT ISR stubs for exceptions and IRQs
; All stubs push vector + error code and call InterruptDispatch(InterruptFrame*)

bits 64
section .text

; InterruptFrame layout (must match struct in idt.cpp):
; r15 r14 r13 r12 r11 r10 r9 r8 rbp rsi rdi rdx rcx rbx rax
; vector error_code rip cs rflags rsp ss  (CPU-pushed)

%macro PUSHAQ 0
    push rax
    push rbx
    push rcx
    push rdx
    push rdi
    push rsi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
%endmacro

%macro POPAQ 0
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rsi
    pop rdi
    pop rdx
    pop rcx
    pop rbx
    pop rax
%endmacro

; ISR without error code: push dummy 0 then vector
%macro ISR_NOERR 1
global isr%1
isr%1:
    push qword 0    ; dummy error code
    push qword %1   ; vector
    jmp  isr_common
%endmacro

; ISR with error code: CPU already pushed it, just push vector
%macro ISR_ERR 1
global isr%1
isr%1:
    push qword %1
    jmp  isr_common
%endmacro

; Exceptions
ISR_NOERR 0    ; #DE Divide Error
ISR_NOERR 1    ; #DB Debug
ISR_NOERR 2    ; NMI
ISR_NOERR 3    ; #BP Breakpoint
ISR_NOERR 4    ; #OF Overflow
ISR_NOERR 5    ; #BR Bound Range
ISR_NOERR 6    ; #UD Invalid Opcode
ISR_NOERR 7    ; #NM No Math Coprocessor
ISR_ERR   8    ; #DF Double Fault
ISR_NOERR 9    ; Coprocessor Segment Overrun
ISR_ERR   10   ; #TS Invalid TSS
ISR_ERR   11   ; #NP Segment Not Present
ISR_ERR   12   ; #SS Stack Fault
ISR_ERR   13   ; #GP General Protection
ISR_ERR   14   ; #PF Page Fault
ISR_NOERR 15   ; reserved
ISR_NOERR 16   ; #MF Math Fault
ISR_ERR   17   ; #AC Alignment Check
ISR_NOERR 18   ; #MC Machine Check
ISR_NOERR 19   ; #XF SIMD Floating Point
ISR_NOERR 20
ISR_NOERR 21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_ERR   30   ; #SX Security Exception
ISR_NOERR 31

; IRQs (remapped to 0x20-0x2F)
ISR_NOERR 32
ISR_NOERR 33
ISR_NOERR 34
ISR_NOERR 35
ISR_NOERR 36
ISR_NOERR 37
ISR_NOERR 38
ISR_NOERR 39
ISR_NOERR 40
ISR_NOERR 41
ISR_NOERR 42
ISR_NOERR 43
ISR_NOERR 44
ISR_NOERR 45
ISR_NOERR 46
ISR_NOERR 47

extern InterruptDispatch

isr_common:
    PUSHAQ
    mov  rdi, rsp       ; arg1: pointer to InterruptFrame
    call InterruptDispatch
    POPAQ
    add  rsp, 16        ; pop vector + error_code
    iretq
