;
; KiNBOL Task Scheduler - Low-Level Context Switch & Trampoline
;
; Implements x86_64 context switching (saving/restoring callee-saved registers)
; and task entry trampoline with System V ABI 16-byte stack alignment.
;

[bits 64]

global context_switch
global task_entry_trampoline
extern task_entry_wrapper

section .text

;
; void context_switch(uint64_t **old_rsp, uint64_t *new_rsp, uint64_t new_cr3)
;
; Parameters:
;   RDI = address of old task's saved RSP pointer (&old_task->rsp)
;   RSI = new task's saved RSP pointer (new_task->rsp)
;   RDX = new task's physical CR3 page table base (new_task->cr3)
;
context_switch:
    ; 1. Save current task's callee-saved registers (System V ABI)
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15

    ; TODO: Floating Point / Vector state preservation:
    ; When tasks start using SSE/AVX/FPU, insert FXSAVE64/XSAVE here:
    ;   sub rsp, 512
    ;   fxsave64 [rsp]

    ; 2. Save current stack pointer to *old_rsp
    mov [rdi], rsp

    ; 3. Load new task's stack pointer into RSP
    mov rsp, rsi

    ; TODO: Floating Point / Vector state restoration:
    ;   fxrstor64 [rsp]
    ;   add rsp, 512

    ; 4. Switch CR3 page table if new_cr3 is provided and different
    test rdx, rdx
    jz .skip_cr3
    mov rax, cr3
    cmp rax, rdx
    je .skip_cr3
    mov cr3, rdx
.skip_cr3:

    ; 5. Restore new task's callee-saved registers
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx

    ; 6. Return to new task's saved RIP
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