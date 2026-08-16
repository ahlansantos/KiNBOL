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

    ; The kernel dispatch code is compiled with -msse2 and may use the XMM
    ; registers.  Preserve the user task's SSE state (XMM0-15 + MXCSR) on the
    ; kernel stack across the C call so a user program's floats are intact
    ; when we sysret back out.
    sub rsp, 288
    stmxcsr [rsp + 16]
    movdqu [rsp + 32],  xmm0
    movdqu [rsp + 48],  xmm1
    movdqu [rsp + 64],  xmm2
    movdqu [rsp + 80],  xmm3
    movdqu [rsp + 96],  xmm4
    movdqu [rsp + 112], xmm5
    movdqu [rsp + 128], xmm6
    movdqu [rsp + 144], xmm7
    movdqu [rsp + 160], xmm8
    movdqu [rsp + 176], xmm9
    movdqu [rsp + 192], xmm10
    movdqu [rsp + 208], xmm11
    movdqu [rsp + 224], xmm12
    movdqu [rsp + 240], xmm13
    movdqu [rsp + 256], xmm14
    movdqu [rsp + 272], xmm15

    ; Call C wrapper.
    ; The register frame sits 288 bytes up (the FPU save area is below it),
    ; so point RDI at the regs frame, not at the FPU scratch.
    lea rdi, [rsp + 288]
    call syscall_enter

    movdqu xmm0,  [rsp + 32]
    movdqu xmm1,  [rsp + 48]
    movdqu xmm2,  [rsp + 64]
    movdqu xmm3,  [rsp + 80]
    movdqu xmm4,  [rsp + 96]
    movdqu xmm5,  [rsp + 112]
    movdqu xmm6,  [rsp + 128]
    movdqu xmm7,  [rsp + 144]
    movdqu xmm8,  [rsp + 160]
    movdqu xmm9,  [rsp + 176]
    movdqu xmm10, [rsp + 192]
    movdqu xmm11, [rsp + 208]
    movdqu xmm12, [rsp + 224]
    movdqu xmm13, [rsp + 240]
    movdqu xmm14, [rsp + 256]
    movdqu xmm15, [rsp + 272]
    ldmxcsr [rsp + 16]
    add rsp, 288

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
