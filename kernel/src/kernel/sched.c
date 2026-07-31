#include "sched.h"
#include "dmesg.h"
#include "lock.h"
#include "../mm/heap.h"
#include "../mm/pmm.h"
#include "../mm/vmm.h"
#include "../drivers/serial.h"
#include "pit.h"
#include "gdt.h"

#define TASK_STACK_PAGES 4

extern void task_entry_trampoline(void);

static task_t *head_task    = NULL;
task_t *current_task = NULL;
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
    bkl_acquire();
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
    bkl_release();
}

static void task_list_remove(task_t *task) {
    if (!task || !head_task) return;

    bkl_acquire();
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
    bkl_release();
}

static task_t *task_create_internal(const char *name, task_entry_t entry, void *arg,
                                     pagemap_t pm, bool owns_pagemap) {
    task_t *task = (task_t *)kmalloc(sizeof(task_t));
    if (!task) {
        dmesg("[sched] ERROR: failed to allocate task struct\n");
        return NULL;
    }

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
    task->pagemap      = pm;
    task->owns_pagemap = owns_pagemap;
    task->cr3          = (uint64_t)pm - hhdm_offset;
    sched_strcpy(task->name, name ? name : "task", sizeof(task->name));

    uint64_t *sp = (uint64_t *)((uint8_t *)stack + stack_size);

    sp = (uint64_t *)((uint64_t)sp & ~0xFULL);

    *(--sp) = (uint64_t)arg;
    *(--sp) = (uint64_t)entry;
    *(--sp) = (uint64_t)task_entry_trampoline;

    *(--sp) = 0;
    *(--sp) = 0;
    *(--sp) = 0;
    *(--sp) = 0;
    *(--sp) = 0;
    *(--sp) = 0;

    task->rsp = (uint64_t)sp;
    task->kernel_rsp = (uint64_t)stack + stack_size;

    task_list_insert(task);

    dmesg("[sched] task ");
    dmesg_int(task->id);
    dmesg(" ('");
    dmesg(task->name);
    dmesg(owns_pagemap ? "') created (isolated pagemap)\n" : "') created\n");

    return task;
}

task_t *task_create(const char *name, task_entry_t entry, void *arg) {
    return task_create_internal(name, entry, arg, vmm_current(), false);
}

task_t *task_create_user(const char *name, task_entry_t entry, void *arg) {
    pagemap_t pm = vmm_create_pagemap();
    if (!pm) {
        dmesg("[sched] ERROR: failed to create isolated pagemap for user task\n");
        return NULL;
    }
    task_t *task = task_create_internal(name, entry, arg, pm, true);
    if (!task) {
        vmm_destroy_pagemap(pm);
        return NULL;
    }
    return task;
}

void sched_init(void) {

    task_t *kmain_task = (task_t *)kmalloc(sizeof(task_t));
    if (!kmain_task) return;

    kmain_task->id           = next_pid++;
    kmain_task->state        = TASK_RUNNING;

    void *kmain_phys_stack = pmm_alloc_pages_contiguous(TASK_STACK_PAGES);
    if (kmain_phys_stack) {
        kmain_task->kernel_stack = (void *)pmm_phys_to_virt((uint64_t)kmain_phys_stack);
        kmain_task->stack_size   = TASK_STACK_PAGES * PAGE_SIZE;
        kmain_task->kernel_rsp   = (uint64_t)kmain_task->kernel_stack + kmain_task->stack_size;
        tss_set_rsp0((uint64_t)kmain_task->kernel_stack + kmain_task->stack_size);
    } else {
        kmain_task->kernel_stack = NULL;
        kmain_task->stack_size   = 0;
        dmesg("[sched] WARNING: no RSP0 stack for kmain task, ring3 syscalls will #DF\n");
    }

    kmain_task->entry        = NULL;
    kmain_task->arg          = NULL;
    kmain_task->pagemap      = vmm_current();
    kmain_task->owns_pagemap = false;
    kmain_task->cr3          = (uint64_t)vmm_current() - hhdm_offset;
    sched_strcpy(kmain_task->name, "[kernel]", sizeof(kmain_task->name));

    task_list_insert(kmain_task);
    current_task = kmain_task;

    idle_task = task_create("[idle]", idle_task_entry, NULL);

    bkl_ready = true;
    dmesg("[sched] scheduler online (PID 0 = [kernel], PID 1 = [idle])\n");
}

task_t *sched_current(void) {
    return current_task;
}

void sched_schedule(void) {
    if (!current_task || !head_task) return;

    asm volatile("cli");
    bkl_acquire();

    sched_update_blocked_tasks();

    task_t *start = current_task->next ? current_task : head_task;
    task_t *next  = start->next;

    if (!next) { bkl_release(); asm volatile("sti"); return; }

    task_t *iter = next;
    task_t *chosen = NULL;

    do {
        if (iter != idle_task && iter->state == TASK_READY) {
            chosen = iter;
            break;
        }
        iter = iter->next;
    } while (iter != start);

    if (!chosen) {
        if (idle_task && idle_task->state == TASK_READY) {
            chosen = idle_task;
        } else if (current_task->state == TASK_RUNNING) {
            bkl_release();
            asm volatile("sti");
            return;
        } else {
            chosen = idle_task;
        }
    }

    if (chosen == current_task) { bkl_release(); asm volatile("sti"); return; }

    task_t *old_task = current_task;
    if (old_task->state == TASK_RUNNING) {
        old_task->state = TASK_READY;
    }

    chosen->state  = TASK_RUNNING;
    current_task = chosen;

    if (chosen->kernel_stack)
        tss_set_rsp0((uint64_t)chosen->kernel_stack + chosen->stack_size);

    bkl_release();

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

    asm volatile("sti");
    sched_yield();

    while (1) asm volatile("hlt");
}

task_t *task_get_head(void) {
    return head_task;
}

void task_destroy(task_t *task) {
    if (!task) return;

    uint64_t pages = task->stack_size ? task->stack_size / PAGE_SIZE : TASK_STACK_PAGES;
    pmm_free_pages(task->phys_stack, pages);

    if (task->owns_pagemap && task->pagemap) {
        vmm_destroy_pagemap(task->pagemap);
    }

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