#include "kinlibc.h"

void _main(u64 argc, char **argv) {
    (void)argc; (void)argv;

    kin_print("=== test-openread ===\n");

    i64 fd = kin_open("sda/teste.txt");
    if (fd < 0) {
        kin_print("FAIL: open() returned error ");
        kin_print_int(fd);
        kin_print("\n");
        kin_exit(1);
    }
    kin_print("PASS: open() fd="); kin_print_int(fd); kin_print("\n");

    char buf[128];
    i64 got = kin_read(fd, buf, sizeof(buf) - 1);
    if (got < 0) {
        kin_print("FAIL: read() returned error\n");
        kin_exit(1);
    }
    buf[got] = 0;
    kin_print("PASS: read() got "); kin_print_int(got); kin_print(" bytes: "); kin_print(buf); kin_print("\n");

    if (kin_close(fd) != 0) {
        kin_print("FAIL: close() returned error\n");
        kin_exit(1);
    }
    kin_print("PASS: close() ok\n");

    kin_print("=== test-openread done ===\n");
    kin_exit(0);
}

KIN_MAIN_TRAMPOLINE