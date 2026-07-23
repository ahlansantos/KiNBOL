#include "lock.h"

spinlock_t bkl = SPINLOCK_INIT;

volatile bool bkl_ready = false;