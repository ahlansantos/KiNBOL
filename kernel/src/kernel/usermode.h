#ifndef USERMODE_H
#define USERMODE_H

#include <stdint.h>
#include "sched.h"

__attribute__((noreturn)) void enter_userspace(uint64_t rip, uint64_t rsp);


void syscall_init(void);
void usertest_run(void);
task_t *usertest_launch(void);

#endif