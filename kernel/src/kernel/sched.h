#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "../mm/vmm.h"

#define STACK_SIZE 16384

#define TASK_MAX_FDS 16

struct vfs_node;

typedef struct {
    struct vfs_node *node;
    uint32_t         offset;
    int               in_use;
} task_fd_t;

typedef enum {
    TASK_READY,
    TASK_RUNNING,
    TASK_BLOCKED,
    TASK_DEAD
} task_state_t;

typedef void (*task_entry_t)(void *arg);

typedef struct task {
    uint64_t       rsp;
    uint64_t       cr3;
    uint32_t       id;
    char           name[32];
    task_state_t   state;
    uint64_t       wake_time_ms;
    void          *phys_stack;
    void          *kernel_stack;
    size_t         stack_size;
    task_entry_t   entry;
    void          *arg;
    pagemap_t      pagemap;
    bool           owns_pagemap;
    struct task   *prev;
    struct task   *next;
    uint64_t       user_rsp;
    uint64_t       kernel_rsp;
    uint64_t       user_brk;
    uint64_t       user_mmap_base;
    uint64_t       user_stack_guard_va; 
    task_fd_t      fds[TASK_MAX_FDS];
} task_t;

void sched_init(void);

task_t *task_create(const char *name, task_entry_t entry, void *arg);

task_t *task_create_user(const char *name, task_entry_t entry, void *arg);

void sched_yield(void);

void sched_schedule(void);

task_t *sched_current(void);

void task_exit(void);

void task_destroy(task_t *task);

void task_reaper(void);

void task_entry_wrapper(task_entry_t entry, void *arg);

void sched_update_blocked_tasks(void);

task_t *task_get_head(void);

extern void context_switch(uint64_t *old_rsp, uint64_t new_rsp, uint64_t new_cr3);