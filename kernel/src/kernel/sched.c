

#include "sched.h"
#include "dmesg.h"
#include "../mm/heap.h"
#include "../mm/pmm.h"
#include "../mm/vmm.h"
#include "../drivers/serial.h"
#include "pit.h"

/* 16 KB stacks (4 contiguous pages) via HHDM. Needs phys contiguity
 * since there's no VMALLOC region yet — temporary limitation. */
#define TASK_STACK_PAGES 4


extern void task_entry_trampoline(void);

static task_t *head_task    = NULL;
static task_t *current_task = NULL;
static task_t *idle_task    = NULL;
static uint32_t next_pid    = 0;

extern uint64_t hhdm_offset;

static void sched_strcpy(char *dst, const char *src, size_t max_len) {
    size_t i = 0;
    if (!dst || !src || max_len == 0) return;
    while (src[i] && i < max_len - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void idle_task_entry(void *arg) {
    (void)arg;
    while (1) {
        task_reaper();
        asm volatile("hlt");
        sched_yield();
    }
}

void task_entry_wrapper(task_entry_t entry, void *arg) {
    if (entry) {
        entry(arg);
    }
    task_exit();
}

static void task_list_insert(task_t *task) {
    if (!head_task) {
        head_task = task;
        task->next = task;
        task->prev = task;
    } else {
        task_t *tail = head_task->prev;
        tail->next = task;
        task->prev = tail;
        task->next = head_task;
        head_task->prev = task;
    }
}

static void task_list_remove(task_t *task) {
    if (!task || !head_task) return;

    if (task->next == task) {
        head_task = NULL;
    } else {
        task->prev->next = task->next;
        task->next->prev = task->prev;
        if (head_task == task) {
            head_task = task->next;
        }
    }
    task->next = NULL;
    task->prev = NULL;
}

task_t *task_create(const char *name, task_entry_t entry, void *arg) {
    task_t *task = (task_t *)kmalloc(sizeof(task_t));
    if (!task) {
        dmesg("[sched] ERROR: failed to allocate task struct\n");
        return NULL;
    }

    /* Allocate 16 KB contiguous physical stack (4 pages).
     * If this fails, the task cannot be created — a 4 KB stack would
     * overflow with any non-trivial call chain. */
    void *phys_stack = pmm_alloc_pages_contiguous(TASK_STACK_PAGES);
    if (!phys_stack) {
        dmesg("[sched] ERROR: cannot allocate 16 KB contiguous stack for task\n");
        kfree(task);
        return NULL;
    }
    uint64_t stack_size = TASK_STACK_PAGES * PAGE_SIZE;

    void *stack = (void *)pmm_phys_to_virt((uint64_t)phys_stack);

    task->id           = next_pid++;
    task->state        = TASK_READY;
    task->phys_stack   = phys_stack;
    task->kernel_stack = stack;
    task->stack_size   = stack_size;
    task->entry        = entry;
    task->arg          = arg;
    task->cr3          = (uint64_t)vmm_current() - hhdm_offset;
    sched_strcpy(task->name, name ? name : "task", sizeof(task->name));

    /* Set up stack frame according to System V ABI (16-byte alignment) */
    uint64_t *sp = (uint64_t *)((uint8_t *)stack + stack_size);

    /* Align stack pointer to 16 bytes */
    sp = (uint64_t *)((uint64_t)sp & ~0xFULL);

    /* Arguments for task_entry_trampoline */
    *(--sp) = (uint64_t)arg;                     /* Second pop in trampoline (RSI) */
    *(--sp) = (uint64_t)entry;                   /* First pop in trampoline (RDI)  */
    *(--sp) = (uint64_t)task_entry_trampoline;   /* Return address for context_switch 'ret' */

    /* Saved callee-saved registers popped by context_switch */
    *(--sp) = 0; /* R15 */
    *(--sp) = 0; /* R14 */
    *(--sp) = 0; /* R13 */
    *(--sp) = 0; /* R12 */
    *(--sp) = 0; /* RBP */
    *(--sp) = 0; /* RBX */

    task->rsp = (uint64_t)sp;

    task_list_insert(task);

    dmesg("[sched] task ");
    dmesg_int(task->id);
    dmesg(" ('");
    dmesg(task->name);
    dmesg("') created\n");

    return task;
}

void sched_init(void) {
    /* Wrap the currently executing kmain thread as PID 0 */
    task_t *kmain_task = (task_t *)kmalloc(sizeof(task_t));
    if (!kmain_task) return;

    kmain_task->id           = next_pid++;
    kmain_task->state        = TASK_RUNNING;
    kmain_task->kernel_stack = NULL;
    kmain_task->stack_size   = 0;
    kmain_task->entry        = NULL;
    kmain_task->arg          = NULL;
    kmain_task->cr3          = (uint64_t)vmm_current() - hhdm_offset;
    sched_strcpy(kmain_task->name, "[kernel]", sizeof(kmain_task->name));

    task_list_insert(kmain_task);
    current_task = kmain_task;

    /* Create idle task (PID 1) */
    idle_task = task_create("[idle]", idle_task_entry, NULL);

    dmesg("[sched] scheduler online (PID 0 = [kernel], PID 1 = [idle])\n");
}

task_t *sched_current(void) {
    return current_task;
}

void sched_schedule(void) {
    if (!current_task || !head_task) return;

    sched_update_blocked_tasks();

    task_t *start = current_task->next ? current_task : head_task;
    task_t *next  = start->next;
    
    if (!next) return;

    task_t *iter = next;
    task_t *chosen = NULL;

    /* Find next READY task in round-robin fashion */
    do {
        if (iter != idle_task && iter->state == TASK_READY) {
            chosen = iter;
            break;
        }
        iter = iter->next;
    } while (iter != start);

    /* Fallback to idle_task if no other task is READY */
    if (!chosen) {
        if (idle_task && idle_task->state == TASK_READY) {
            chosen = idle_task;
        } else if (current_task->state == TASK_RUNNING) {
            return; /* Continue running current task */
        } else {
            chosen = idle_task;
        }
    }

    if (chosen == current_task) return;

    task_t *old_task = current_task;
    if (old_task->state == TASK_RUNNING) {
        old_task->state = TASK_READY;
    }

    chosen->state  = TASK_RUNNING;
    current_task = chosen;

    context_switch(&old_task->rsp, chosen->rsp, chosen->cr3);
}

void sched_yield(void) {
    sched_schedule();
}

void task_exit(void) {
    if (!current_task) return;

    dmesg("[sched] task ");
    dmesg_int(current_task->id);
    dmesg(" ('");
    dmesg(current_task->name);
    dmesg("') exited\n");

    current_task->state = TASK_DEAD;

    /* Note: Dead tasks are now cleaned up by the task reaper in the idle task. */
    sched_yield();

    /* Should never reach here */
    while (1) asm volatile("hlt");
}

task_t *task_get_head(void) {
    return head_task;
}

void task_destroy(task_t *task) {
    if (!task) return;
    /* All task stacks are 16 KB (4 pages). kmain has stack_size=0,
     * so guard against division by zero. */
    uint64_t pages = task->stack_size ? task->stack_size / PAGE_SIZE : TASK_STACK_PAGES;
    pmm_free_pages(task->phys_stack, pages);
    kfree(task);
}

void task_reaper(void) {
    bool cleaned_any = false;
restart:
    if (!head_task) {
        if (cleaned_any) dmesg("[reaper] cleanup complete\n");
        return;
    }

    task_t *iter = head_task;

    do {
        task_t *next = iter->next;
        if (iter->state == TASK_DEAD && iter != current_task) {
            dmesg("[reaper] task ");
            dmesg_int(iter->id);
            dmesg(" destroyed\n");
            
            task_list_remove(iter);
            task_destroy(iter);
            cleaned_any = true;
            
            goto restart; 
        }
        iter = next;
    } while (iter && iter != head_task);
    
    if (cleaned_any) {
        dmesg("[reaper] cleanup complete\n");
    }
}

void sched_update_blocked_tasks(void) {
    if (!head_task) return;
    
    uint64_t now = uptime_ms();
    task_t *iter = head_task;
    do {
        if (iter->state == TASK_BLOCKED && iter->wake_time_ms > 0) {
            if (now >= iter->wake_time_ms) {
                iter->state = TASK_READY;
                iter->wake_time_ms = 0;
                dmesg("[sched] task ");
                dmesg_int(iter->id);
                dmesg(" woke up\n");
            }
        }
        iter = iter->next;
    } while (iter && iter != head_task);
}
