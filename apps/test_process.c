#include "cervus_user.h"

__attribute__((naked)) void _start(void) {
    asm volatile("mov %%rsp,%%rdi; and $-16,%%rsp; call test_main; ud2":::  "memory");
}

static int failed = 0;
static void ok(const char *s)   { printf("  [OK]  %s\n", s); }
static void fail(const char *s) { printf("  [FAIL] %s\n", s); failed = 1; }

void test_main(uint64_t *sp) {
    (void)sp;
    puts("--- test_process ---");

    pid_t my_pid = getpid();
    if (my_pid > 0) ok("getpid > 0");
    else fail("getpid > 0");

    pid_t my_ppid = getppid();
    if (my_ppid > 0) ok("getppid > 0");
    else fail("getppid > 0");

    pid_t child = fork();
    if (child < 0) { fail("fork"); exit(1); }

    if (child == 0) {
        if (getppid() == my_pid) ok("child: ppid == parent pid");
        else fail("child: ppid == parent pid");
        exit(42);
    }

    int status = 0;
    pid_t reaped = waitpid(child, &status, 0);
    if (reaped == child)           ok("waitpid returns child pid");
    else                           fail("waitpid returns child pid");
    if (WIFEXITED(status))         ok("WIFEXITED");
    else                           fail("WIFEXITED");
    if (WEXITSTATUS(status) == 42) ok("exit code 42");
    else { printf("  exit code = %d\n", WEXITSTATUS(status)); fail("exit code 42"); }

    pid_t r = waitpid(-1, &status, WNOHANG);
    if (r == 0) ok("WNOHANG returns 0 when no zombie");
    else fail("WNOHANG returns 0 when no zombie");

    if (getuid() == 0) ok("getuid == 0 (root)");
    else fail("getuid == 0");

    puts("--- test_process done ---");
    exit(failed ? 1 : 0);
}