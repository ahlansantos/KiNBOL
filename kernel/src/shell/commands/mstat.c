#include "../commands.h"
#include "util.h"
#include "../../graphics/terminal.h"
#include "../../drivers/mouse.h"
#include "../../drivers/keyboard.h"
#include "../../kernel/sched.h"

#define KEY_ESC 0x01

void cmd_mstat(void) {
    terminal_println("  mstat: move the mouse / click. ESC to exit.\n");

    int px = 0, py = 0;

    while (!keyboard_held(KEY_ESC)) {
        keyboard_update();
        mouse_state_t m = mouse_get_state();

        if (m.dx || m.dy || m.left || m.right || m.middle) {
            px += m.dx;
            py += m.dy;

            terminal_set_fg(COLOR_BODY);
            terminal_print("  dx=");     terminal_print_int(m.dx);
            terminal_print(" dy=");      terminal_print_int(m.dy);
            terminal_print(" pos=(");    terminal_print_int(px);
            terminal_print(",");         terminal_print_int(py);
            terminal_print(") L=");      terminal_print_int(m.left);
            terminal_print(" R=");       terminal_print_int(m.right);
            terminal_print(" M=");       terminal_print_int(m.middle);
            terminal_println("");
        }

        sched_yield();
    }

    terminal_println("\n  mstat end.");
}