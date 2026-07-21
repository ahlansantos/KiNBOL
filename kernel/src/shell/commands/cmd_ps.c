/*
 * KiNBOL Shell Command: ps (Process Status)
 *
 * Lists all active tasks in the kernel, their PID, state, stack parameters, and entry points.
 */

#include "../commands.h"
#include "util.h"
#include "../../graphics/terminal.h"
#include "../../kernel/sched.h"

static const char *state_to_str(task_state_t state) {
    switch (state) {
        case TASK_RUNNING: return "RUNNING";
        case TASK_READY:   return "READY";
        case TASK_BLOCKED: return "BLOCKED";
        case TASK_DEAD:    return "DEAD";
        default:           return "UNKNOWN";
    }
}

void cmd_ps(void) {
    task_t *head = task_get_head();
    if (!head) {
        terminal_set_fg(COLOR_ERROR);
        terminal_println("  No active tasks.");
        return;
    }

    terminal_set_fg(COLOR_HEADER);
    terminal_println("\n  PID   NAME            STATE       STACK_BASE          RSP");
    terminal_set_fg(COLOR_DIM);
    terminal_println("  ------------------------------------------------------------------");

    task_t *curr = head;
    do {
        terminal_set_fg(COLOR_BODY);

        /* PID */
        terminal_print("  ");
        if (curr->id < 10) terminal_print(" ");
        terminal_print_int(curr->id);
        terminal_print("    ");

        /* NAME */
        terminal_print(curr->name);
        size_t len = 0;
        while (curr->name[len]) len++;
        for (size_t i = len; i < 16; i++) terminal_print(" ");

        /* STATE */
        if (curr->state == TASK_RUNNING)      terminal_set_fg(COLOR_SUCCESS);
        else if (curr->state == TASK_READY)   terminal_set_fg(COLOR_ACCENT);
        else                                  terminal_set_fg(COLOR_DIM);

        const char *st = state_to_str(curr->state);
        terminal_print(st);
        len = 0; while (st[len]) len++;
        for (size_t i = len; i < 12; i++) terminal_print(" ");

        terminal_set_fg(COLOR_BODY);

        /* STACK BASE & RSP */
        terminal_print_hex((uint64_t)curr->kernel_stack);
        terminal_print("  ");
        terminal_print_hex(curr->rsp);
        terminal_println("");

        curr = curr->next;
    } while (curr != head);

    terminal_println("");
}
