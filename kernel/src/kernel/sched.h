/*
 * KiNBOL Task Scheduler & Thread Management
 *
 * Implements cooperative & preemptive process/thread management using a
 * doubly-linked circular task list. Adheres strictly to the 16-byte SysV
 * x86_64 ABI stack alignment for C entry points.
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define STACK_SIZE 16384 /* 16KB Kernel Stack per Task */

typedef enum {
    TASK_READY,
    TASK_RUNNING,
    TASK_BLOCKED,
    TASK_DEAD
} task_state_t;

typedef void (*task_entry_t)(void *arg);

typedef struct task {
    uint64_t       rsp;          /* Current saved stack pointer (must be offset 0 for assembly) */
    uint64_t       cr3;          /* Physical page table address */
    uint32_t       id;           /* Process ID (PID) */
    char           name[32];     /* Task name for debugging/ps */
    task_state_t   state;        /* Current state */
    void          *phys_stack;   /* Physical address of the stack page */
    void          *kernel_stack; /* Allocated stack memory base pointer */
    size_t         stack_size;   /* Stack size in bytes */
    task_entry_t   entry;        /* Function entry point */
    void          *arg;          /* Function argument */
    struct task   *prev;         /* Doubly-linked task list: previous */
    struct task   *next;         /* Doubly-linked task list: next */
} task_t;

/* Initialize task management subsystem (wraps kmain as PID 0, spawns idle PID 1) */
void sched_init(void);

/* Create a new kernel task */
task_t *task_create(const char *name, task_entry_t entry, void *arg);

/* Yield CPU to the next ready task */
void sched_yield(void);

/* Perform context switch if another task is ready */
void sched_schedule(void);

/* Get currently running task */
task_t *sched_current(void);

/* Terminate the current task */
void task_exit(void);

/* Destroy a dead task and free its resources */
void task_destroy(task_t *task);

/* Reaper function to clean up dead tasks */
void task_reaper(void);

/* Internal entry trampoline for new tasks */
void task_entry_wrapper(task_entry_t entry, void *arg);

/* Get the head of the task list for task enumeration (ps) */
task_t *task_get_head(void);

/* Assembly low-level context switch routine */
extern void context_switch(uint64_t *old_rsp, uint64_t new_rsp, uint64_t new_cr3);
