#include "util.h"
#include "../../graphics/terminal.h"
#include "../../kernel/pit.h"

uint32_t get_ticks(void) {
    return (uint32_t)(uptime_ms() / 10);
}

int str_len(const char *s) {
    int i = 0;
    while (s[i]) i++;
    return i;
}

void print_header(const char *title) {
    terminal_set_fg(COLOR_HEADER);
    terminal_println("");
    terminal_print("  ─── ");
    terminal_print(title);
    terminal_println(" ───");
    terminal_set_fg(COLOR_WHITE);
}

void print_separator(void) {
    terminal_set_fg(COLOR_DIM);
    terminal_println("  ────────────────────────────────────────");
    terminal_set_fg(COLOR_WHITE);
}

void print_field(const char *label, const char *value) {
    terminal_set_fg(COLOR_BODY);
    terminal_print("  ");
    terminal_print(label);
    terminal_set_fg(COLOR_HIGHLIGHT);
    terminal_println(value);
}

static const char *calc_ptr;

static uint64_t calc_num(void) {
    while (*calc_ptr == ' ') calc_ptr++;
    uint64_t val = 0;
    if (calc_ptr[0] == '0' && (calc_ptr[1] == 'x' || calc_ptr[1] == 'X')) {
        calc_ptr += 2;
        while ((*calc_ptr >= '0' && *calc_ptr <= '9') ||
               (*calc_ptr >= 'a' && *calc_ptr <= 'f') ||
               (*calc_ptr >= 'A' && *calc_ptr <= 'F')) {
            char c = *calc_ptr++;
            if      (c >= '0' && c <= '9') val = val * 16 + (c - '0');
            else if (c >= 'a' && c <= 'f') val = val * 16 + (c - 'a' + 10);
            else                           val = val * 16 + (c - 'A' + 10);
        }
    } else {
        while (*calc_ptr >= '0' && *calc_ptr <= '9')
            val = val * 10 + (*calc_ptr++) - '0';
    }
    return val;
}

static uint64_t calc_primary(void) {
    while (*calc_ptr == ' ') calc_ptr++;
    if (*calc_ptr == '(') {
        calc_ptr++;
        uint64_t v = calc_expr();
        if (*calc_ptr == ')') calc_ptr++;
        return v;
    }
    if (*calc_ptr == '~') { calc_ptr++; return ~calc_primary(); }
    if (*calc_ptr == '-') { calc_ptr++; return (uint64_t)(-(int64_t)calc_primary()); }
    return calc_num();
}

static uint64_t calc_mul(void) {
    uint64_t v = calc_primary();
    while (1) {
        while (*calc_ptr == ' ') calc_ptr++;
        if (*calc_ptr == '*') { calc_ptr++; v *= calc_primary(); }
        else if (*calc_ptr == '/') {
            calc_ptr++;
            uint64_t d = calc_primary();
            v = d ? v / d : 0;
        }
        else if (*calc_ptr == '%') {
            calc_ptr++;
            uint64_t d = calc_primary();
            v = d ? v % d : 0;
        }
        else break;
    }
    return v;
}

static uint64_t calc_add(void) {
    uint64_t v = calc_mul();
    while (1) {
        while (*calc_ptr == ' ') calc_ptr++;
        if (*calc_ptr == '+') { calc_ptr++; v += calc_mul(); }
        else if (*calc_ptr == '-') { calc_ptr++; v -= calc_mul(); }
        else break;
    }
    return v;
}

static uint64_t calc_shift(void) {
    uint64_t v = calc_add();
    while (1) {
        while (*calc_ptr == ' ') calc_ptr++;
        if (calc_ptr[0] == '<' && calc_ptr[1] == '<') {
            calc_ptr += 2;
            v <<= calc_add();
        }
        else if (calc_ptr[0] == '>' && calc_ptr[1] == '>') {
            calc_ptr += 2;
            v >>= calc_add();
        }
        else break;
    }
    return v;
}

static uint64_t calc_band(void) {
    uint64_t v = calc_shift();
    while (*calc_ptr == '&' && calc_ptr[1] != '&') {
        calc_ptr++;
        v &= calc_shift();
    }
    return v;
}

static uint64_t calc_bxor(void) {
    uint64_t v = calc_band();
    while (*calc_ptr == '^') { calc_ptr++; v ^= calc_band(); }
    return v;
}

static uint64_t calc_bor(void) {
    uint64_t v = calc_bxor();
    while (*calc_ptr == '|' && calc_ptr[1] != '|') {
        calc_ptr++;
        v |= calc_bxor();
    }
    return v;
}

void calc_set_input(const char *expr) {
    calc_ptr = expr;
}

uint64_t calc_expr(void) {
    return calc_bor();
}