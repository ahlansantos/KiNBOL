#ifndef LOCK_H
#define LOCK_H

#include <stdint.h>
#include <stdbool.h>

typedef volatile int spinlock_t;

#define SPINLOCK_INIT 0

static inline void spin_acquire(spinlock_t *lock) {
    int val = 1;
    for (;;) {

        while (*lock)
            asm volatile("pause");

        asm volatile("lock xchgl %0, %1"
                     : "+r"(val), "+m"(*lock)
                     :
                     : "memory");
        if (val == 0)
            break;
        val = 1;
    }
}

static inline void spin_release(spinlock_t *lock) {
    asm volatile("" ::: "memory");
    *lock = 0;
}

extern spinlock_t bkl;
extern volatile bool bkl_ready;

static inline void bkl_acquire(void) {
    if (__builtin_expect(!bkl_ready, 0)) return;
    spin_acquire(&bkl);
}

static inline void bkl_release(void) {
    if (__builtin_expect(!bkl_ready, 0)) return;
    spin_release(&bkl);
}

extern spinlock_t fs_lock;

static inline void fs_lock_acquire(void) {
    spin_acquire(&fs_lock);
}

static inline void fs_lock_release(void) {
    spin_release(&fs_lock);
}

#endif