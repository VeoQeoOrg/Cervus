#include "cervus_user.h"

static void pr(const char *s)  { write(1, s, strlen(s)); }
static void prln(const char *s){ pr(s); pr("\n"); }

static void prd(uint64_t v) {
    char tmp[24]; int i = 23;
    tmp[i] = '\0';
    if (v == 0) { pr("0"); return; }
    while (v) { tmp[--i] = '0' + (v % 10); v /= 10; }
    pr(tmp + i);
}

static void _start_main(uint64_t *initial_rsp) {
    uint64_t  argc =  initial_rsp[0];
    char    **argv = (char **)&initial_rsp[1];

    prln("=== execve_target: STARTED ===");
    pr("  PID:  "); prd(getpid());  pr("\n");
    pr("  PPID: "); prd(getppid()); pr("\n");
    pr("  argc: "); prd(argc);      pr("\n");

    for (uint64_t i = 0; i < argc; i++) {
        pr("  argv["); prd(i); pr("] = ");
        pr((const char *)argv[i]);
        pr("\n");
    }

    prln("=== execve_target: DONE, exit(99) ===");
    exit(99);
}

__attribute__((naked)) void _start(void) {
    asm volatile (
        "mov  %%rsp, %%rdi\n\t"
        "and  $-16, %%rsp\n\t"
        "call _start_main\n\t"
        "ud2\n\t"
        ::: "memory"
    );
}