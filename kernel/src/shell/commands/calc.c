<<<<<<< HEAD
/*
 * The 'calc' shell command: takes an expression, evaluates it with
 * calc_expr(), and prints the result both in hex and in decimal.
 */
=======
>>>>>>> origin/x86_64-uefi
#include "../commands.h"
#include "util.h"
#include "../../graphics/terminal.h"

void cmd_calc(const char *expr) {
    calc_set_input(expr);
    uint64_t result = calc_expr();

    terminal_set_fg(COLOR_HEADER);
    terminal_print("\n  = ");
    terminal_set_fg(COLOR_HIGHLIGHT);
    terminal_print_hex(result);
    terminal_set_fg(COLOR_BODY);
    terminal_print("  (dec: ");
    terminal_print_int((uint32_t)result);
    terminal_println(")\n");
}