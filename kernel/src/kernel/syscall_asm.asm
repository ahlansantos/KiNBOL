;
; KiNBOL syscall entry point
;

[bits 64]

global syscall_entry
global syscall_scratch

extern current_task
extern syscall_enter

section .data
syscall_scratch: dq 0

section .text

syscall_entry:
    ; syscall instruction saves RIP to RCX and RFLAGS to R11.
    ; RSP is not automatically changed to a kernel stack!
    ; We must manually switch to the kernel stack.
    ; In a single-core environment, we can use a global scratch variable
    ; to temporarily save RAX so we can load the current task pointer.

    mov [rel syscall_scratch], rax

    ; Load current_task pointer into RAX
    mov rax, [rel current_task]

    ; current_task->user_rsp = rsp
    ; user_rsp is at offset 136
    mov [rax + 136], rsp

    ; rsp = current_task->kernel_rsp
    ; kernel_rsp is at offset 144
    mov rsp, [rax + 144]

    ; Restore RAX
    mov rax, [rel syscall_scratch]

    ; Build syscall_regs frame
    ; enum { R15=0, R14, R13, R12, R11, R10, R9, R8, RBP, RDI, RSI, RDX, RCX, RBX, RAX };
    push 0      ; padding to keep RSP 16-byte aligned before 'call' (16 pushes = 128 bytes)
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    ; Call C wrapper
    mov rdi, rsp
    call syscall_enter

    ; Restore registers
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    add rsp, 8  ; remove padding

    ; We must restore user RSP
    ; Save RAX again to scratch
    mov [rel syscall_scratch], rax
    mov rax, [rel current_task]
    mov rsp, [rax + 136]
    mov rax, [rel syscall_scratch]

    ; Return to ring 3
    ; sysret uses RCX for RIP and R11 for RFLAGS
    ; We MUST use o64 sysret in NASM to emit the 64-bit sysret instruction,
    ; otherwise it emits the 32-bit sysret and drops to compatibility mode!
    o64 sysret
