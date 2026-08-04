#ifndef USERMODE_H
#define USERMODE_H

#include <stdint.h>
#include "sched.h"

__attribute__((noreturn)) void enter_userspace(uint64_t rip, uint64_t rsp);

#define USER_CODE_VA    0x400000ULL
#define USER_STACK_VA   0x401000ULL
#define USER_HEAP_START 0x80000000ULL
#define USER_MMAP_START 0x700000000000ULL
#define USER_STACK_TOP  0x7FFFFFFFF000ULL


void syscall_init(void);
void usertest_run(void);
task_t *usertest_launch(void);

void exec_run(void *arg);
task_t *exec_launch(const char *path);

#endif