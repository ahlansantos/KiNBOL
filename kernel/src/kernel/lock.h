/*
 * KiNBOL Kernel Locking Primitives
 *
 * Provides a simple spinlock using x86 `lock xchg` and a Big Kernel Lock
 * (BKL) as the foundation for preemptive scheduling. The BKL is a single
 * global spinlock that every kernel operation must acquire before touching
 * shared state (PMM, heap, scheduler, terminal, etc.).
 *
 * Usage (BKL):
 *   bkl_acquire();
 *   // ... critical section ...
 *   bkl_release();
 *
 * Usage (spinlock):
 *   spinlock_t mylock = SPINLOCK_INIT;
 *   spin_acquire(&mylock);
 *   // ... critical section ...
 *   spin_release(&mylock);
 *
 * For the early boot sequence (before the scheduler is online), the BKL
 * is a no-op — there's only one CPU and no preemption to worry about.
 * The scheduler sets bkl_ready = true after sched_init() completes.
 *
 * NOTE: No <stdatomic.h> here — this is a freestanding kernel environment.
 * All atomicity is provided by the x86 `lock` prefix on xchg.
 */

#ifndef LOCK_H
#define LOCK_H

#include <stdint.h>
#include <stdbool.h>

/* ------------------------------------------------------------------ */
/*  Spinlock                                                          */
/* ------------------------------------------------------------------ */

typedef volatile int spinlock_t;

#define SPINLOCK_INIT 0

/* Acquire spinlock via atomic xchg (implicitly locked on x86).
 * Spins until lock is acquired. Disabling interrupts is the caller's
 * responsibility if the lock is shared with an interrupt handler. */
static inline void spin_acquire(spinlock_t *lock) {
    int val = 1;
    for (;;) {
        /* Spin while locked, with pause for hyperthread efficiency */
        while (*lock)
            asm volatile("pause");
        /* Attempt atomic exchange: if lock was 0, we now own it */
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
    asm volatile("" ::: "memory"); /* barrier */
    *lock = 0;
}

/* ------------------------------------------------------------------ */
/*  Big Kernel Lock (BKL)                                             */
/* ------------------------------------------------------------------ */

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

#endif /* LOCK_H */