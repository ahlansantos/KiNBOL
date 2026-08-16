;
; KiNBOL Task Scheduler - Low-Level Context Switch & Trampoline
;
; Implements x86_64 context switching (saving/restoring callee-saved registers)
; and task entry trampoline with System V ABI 16-byte stack alignment.
;

[bits 64]

global context_switch
global task_entry_trampoline
global fpu_enable

extern task_entry_wrapper

section .text

;
; void fpu_enable(void)
;
; Boot-time FPU + SSE enablement (see fpu.h). Kept in raw assembly so that no
; compiler-generated SSE instruction can execute before the CPU is configured.
;
fpu_enable:
    ; CR0: clear EM(2) and TS(3), set MP(1) so the x87/SSE units run normally.
    mov rax, cr0
    and rax, ~((1 << 2) | (1 << 3))
    or  rax, (1 << 1)
    mov cr0, rax

    ; CR4: OSFXSR(9) and OSXMMEXCPT(10).
    mov rax, cr4
    or  rax, (1 << 9) | (1 << 10)
    mov cr4, rax

    ; If XSAVE is advertised (CPUID.1:ECX bit 26), also enable OSXSAVE and
    ; program XCR0 so the OS can manage x87 + SSE state in the future.
    mov eax, 1
    cpuid
    test ecx, (1 << 26)
    jz   .no_xsave
    mov rax, cr4
    or  rax, (1 << 18)          ; CR4.OSXSAVE
    mov cr4, rax
    xor ecx, ecx                ; XCR0 index 0
    xor edx, edx
    mov eax, 0x3                ; XCR0_X87 | XCR0_SSE
    xsetbv
.no_xsave:
    ; Reset the x87 unit; MXCSR stays at its power-on default.
    fninit
    ret

;
; void context_switch(uint64_t **old_rsp, uint64_t *new_rsp, uint64_t new_cr3,
;                     void *old_fpu, void *new_fpu)
;
; Parameters (System V):
;   RDI = address of old task's saved RSP pointer (&old_task->rsp)
;   RSI = new task's saved RSP pointer (new_task->rsp)
;   RDX = new task's physical CR3 page table base (new_task->cr3)
;   RCX = old task's 512-byte, 16-aligned FPU/SSE state buffer
;   R8  = new task's 512-byte, 16-aligned FPU/SSE state buffer
;
; Saves the current (old) task's FPU/SSE state with FXSAVE64 and restores the
; new task's state with FXRSTOR64, wrapping the usual callee-saved registers
; and CR3 switch.
;
context_switch:
    ; 1. Save current task's callee-saved registers (System V ABI)
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15

    ; 2. Snapshot the old task's FPU/SSE state (16-byte aligned buffer).
    test rcx, rcx
    jz   .skip_save_fpu
    fxsave64 [rcx]
.skip_save_fpu:

    ; 3. Save current stack pointer to *old_rsp, then load the new one.
    mov [rdi], rsp
    mov rsp, rsi

    ; 4. Restore the new task's FPU/SSE state.
    test r8, r8
    jz   .skip_rest_fpu
    fxrstor64 [r8]
.skip_rest_fpu:

    ; 5. Switch CR3 page table if new_cr3 is provided and different
    test rdx, rdx
    jz   .skip_cr3
    mov rax, cr3
    cmp rax, rdx
    je   .skip_cr3
    mov cr3, rdx
.skip_cr3:

    ; 6. Restore new task's callee-saved registers
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx

    sti

    ; 7. Return to new task's saved RIP
    ret

;
; Trampoline executed when context_switch returns into a newly created task.
; The initial stack contains: [entry], [arg]
;
task_entry_trampoline:
    sti        ; a new task never resumes through sched_schedule's own
               ; post-switch 'sti' (it starts here instead), so without
               ; this it could inherit IF=0 from whatever context_switch
               ; happened to interrupt when it was first spawned.
    pop rdi    ; First argument for task_entry_wrapper: task_entry_t entry
    pop rsi    ; Second argument for task_entry_wrapper: void *arg

    ; Call the C wrapper. RSP is 16-byte aligned here, so 'call' pushes RIP (8B)
    ; leaving RSP % 16 == 8 inside task_entry_wrapper, exactly as required by SysV ABI.
    mov rax, task_entry_wrapper
    call rax

    ; If task_entry_wrapper ever returns, halt cleanly
    cli
.dead_loop:
    hlt
    jmp .dead_loop