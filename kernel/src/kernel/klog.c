#include <stddef.h>
#include "klog.h"
#include "dmesg.h"
#include "pit.h"
#include "../graphics/terminal.h"
#include "../shell/commands/util.h"

static klog_level_t screen_min_level = KLOG_INFO;

void klog_init(void) {
    screen_min_level = KLOG_INFO;
}

void klog_set_screen_level(klog_level_t min_level) {
    screen_min_level = min_level;
}

static const char *level_tag[] = {
    "TRACE", "DEBUG", "INFO ", "WARN ", "ERROR"
};

static uint32_t level_color(klog_level_t level) {
    switch (level) {
        case KLOG_TRACE: return COLOR_DIM;
        case KLOG_DEBUG: return COLOR_DIM;
        case KLOG_INFO:  return COLOR_BODY;
        case KLOG_WARN:  return COLOR_WARNING;
        case KLOG_ERROR: return COLOR_ERROR;
        default:         return COLOR_BODY;
    }
}

typedef void (*klog_sink_t)(char c, void *ctx);

static void emit_str(klog_sink_t sink, void *ctx, const char *s) {
    if (!s) s = "(null)";
    for (int i = 0; s[i]; i++) sink(s[i], ctx);
}

static void emit_uint(klog_sink_t sink, void *ctx, uint64_t n, int base, bool upper) {
    char tmp[21];
    int i = 20;
    tmp[i] = 0;
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    if (n == 0) {
        sink('0', ctx);
        return;
    }
    while (n) {
        tmp[--i] = digits[n % base];
        n /= base;
    }
    emit_str(sink, ctx, &tmp[i]);
}

static void emit_int(klog_sink_t sink, void *ctx, int64_t n) {
    if (n < 0) {
        sink('-', ctx);
        n = -n;
    }
    emit_uint(sink, ctx, (uint64_t)n, 10, false);
}

static void klog_format(klog_sink_t sink, void *ctx, const char *fmt, va_list ap) {
    for (int i = 0; fmt[i]; i++) {
        if (fmt[i] != '%') {
            sink(fmt[i], ctx);
            continue;
        }
        i++;
        switch (fmt[i]) {
            case 's': emit_str(sink, ctx, va_arg(ap, const char *)); break;
            case 'd': emit_int(sink, ctx, va_arg(ap, int)); break;
            case 'u': emit_uint(sink, ctx, va_arg(ap, unsigned int), 10, false); break;
            case 'x': emit_uint(sink, ctx, va_arg(ap, uint64_t), 16, false); break;
            case 'p':
                emit_str(sink, ctx, "0x");
                emit_uint(sink, ctx, (uint64_t)va_arg(ap, void *), 16, false);
                break;
            case 'c': sink((char)va_arg(ap, int), ctx); break;
            case '%': sink('%', ctx); break;
            case 0:   return;
            default:  sink('%', ctx); sink(fmt[i], ctx); break;
        }
    }
}

static void dmesg_sink(char c, void *ctx) {
    (void)ctx;
    char s[2] = { c, 0 };
    dmesg(s);
}

static void terminal_sink(char c, void *ctx) {
    (void)ctx;
    char s[2] = { c, 0 };
    terminal_print(s);
}

void vklog(klog_level_t level, const char *subsys, const char *fmt, va_list ap) {
    va_list ap_copy;

    dmesg("[");
    dmesg(level_tag[level]);
    dmesg("] ");
    dmesg(subsys);
    dmesg(": ");
    va_copy(ap_copy, ap);
    klog_format(dmesg_sink, NULL, fmt, ap_copy);
    va_end(ap_copy);
    dmesg("\n");

    if (level < screen_min_level) return;

    uint64_t tf = terminal_lock();
    terminal_set_fg(level_color(level));
    terminal_print("[");
    terminal_print_int((uint32_t)uptime_ms());
    terminal_print("ms] [");
    terminal_print(level_tag[level]);
    terminal_print("] ");
    terminal_print(subsys);
    terminal_print(": ");
    va_copy(ap_copy, ap);
    klog_format(terminal_sink, NULL, fmt, ap_copy);
    va_end(ap_copy);
    terminal_println("");
    terminal_unlock(tf);
}

void klog(klog_level_t level, const char *subsys, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vklog(level, subsys, fmt, ap);
    va_end(ap);
}