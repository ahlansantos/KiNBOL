/*
 * Big Kernel Lock (BKL) implementation.
 *
 * The BKL is a single global spinlock that serializes access to all
 * shared kernel state (PMM, heap, scheduler, terminal, etc.). It is
 * the foundation needed before preemptive scheduling can be enabled.
 *
 * During early boot (before sched_init()), bkl_ready is false and the
 * lock is a no-op. Once the scheduler is online, bkl_ready is set to
 * true and every kernel operation must acquire the BKL before touching
 * shared state.
 */

#include "lock.h"

/* The global Big Kernel Lock */
spinlock_t bkl = SPINLOCK_INIT;

/* Flag: set to true by sched_init() once the scheduler is ready.
 * Until then, bkl_acquire/bkl_release are no-ops. */
volatile bool bkl_ready = false;