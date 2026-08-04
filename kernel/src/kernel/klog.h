#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>

typedef enum {
    KLOG_TRACE = 0,
    KLOG_DEBUG,
    KLOG_INFO,
    KLOG_WARN,
    KLOG_ERROR,
} klog_level_t;

void klog_init(void);

void klog_set_screen_level(klog_level_t min_level);

void klog(klog_level_t level, const char *subsys, const char *fmt, ...);
void vklog(klog_level_t level, const char *subsys, const char *fmt, va_list ap);

#define KLOG_T(subsys, ...) klog(KLOG_TRACE, subsys, __VA_ARGS__)
#define KLOG_D(subsys, ...) klog(KLOG_DEBUG, subsys, __VA_ARGS__)
#define KLOG_I(subsys, ...) klog(KLOG_INFO,  subsys, __VA_ARGS__)
#define KLOG_W(subsys, ...) klog(KLOG_WARN,  subsys, __VA_ARGS__)
#define KLOG_E(subsys, ...) klog(KLOG_ERROR, subsys, __VA_ARGS__)