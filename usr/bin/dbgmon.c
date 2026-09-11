#include <stdio.h>
#include <string.h>
#include <sys/cervus.h>

#define MONITOR_VT 1

static const char USAGE[] =
    "Usage: dbgmon\n"
    "Open the kernel debug monitor.\n"
    "\n"
    "It shows the kernel log as it is written, in colour by severity:\n"
    "errors in red, warnings in yellow, successful steps in green.\n"
    "\n"
    "  arrows, PgUp/PgDn   move through the log; new lines keep arriving\n"
    "  G or End            follow the newest line again\n"
    "  /  then text        search, n for the next match\n"
    "  1 2 3 4 5           show only info, warnings, errors, ok or debug\n"
    "  0                   show everything again\n"
    "  q or Esc            come back here\n"
    "\n"
    "Ctrl-Alt-F2 reaches the same screen at any time.\n";

int main(int argc, char **argv) {
    if (argc > 1 && (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help"))) {
        fputs(USAGE, stdout);
        return 0;
    }
    if (cervus_vt_switch(MONITOR_VT) != 0) {
        fprintf(stderr, "dbgmon: cannot reach the monitor console\n");
        return 1;
    }
    return 0;
}
