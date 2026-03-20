#include "cervus_user.h"

__attribute__((naked)) void _start(void) {
    asm volatile("mov %%rsp,%%rdi; and $-16,%%rsp; call test_main; ud2":::  "memory");
}

static int failed = 0;
static void ok(const char *s)   { printf("  [OK]  %s\n", s); }
static void fail(const char *s) { printf("  [FAIL] %s\n", s); failed = 1; }

void test_main(uint64_t *sp) {
    (void)sp;
    puts("--- test_execve ---");

    {
        const char *argv[] = {
            "/bin/target", "hello", "from", "execve", NULL
        };
        pid_t child = fork();
        if (child < 0) { fail("fork"); exit(1); }
        if (child == 0) {
            execve("/bin/target", argv, NULL);
            printf("  [FATAL] execve failed\n");
            exit(127);
        }
        int status = 0;
        waitpid(child, &status, 0);
        if (WIFEXITED(status) && WEXITSTATUS(status) == 99)
            ok("execve target exits 99");
        else {
            printf("  exit status = %d\n", WEXITSTATUS(status));
            fail("execve exit code");
        }
    }

    {
        pid_t child = fork();
        if (child < 0) { fail("fork"); exit(1); }
        if (child == 0) {
            const char *argv[] = { "/no/such/binary", NULL };
            int r = execve("/no/such/binary", argv, NULL);
            exit((r < 0) ? 0 : 1);
        }
        int status = 0;
        waitpid(child, &status, 0);
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
            ok("execve nonexistent returns error");
        else fail("execve nonexistent should return error");
    }

    {
        pid_t child = fork();
        if (child < 0) { fail("fork"); exit(1); }
        if (child == 0) {
            execve("/bin/target", NULL, NULL);
            exit(127);
        }
        int status = 0;
        waitpid(child, &status, 0);
        if (WIFEXITED(status) && WEXITSTATUS(status) == 99)
            ok("execve with NULL argv uses path as argv[0]");
        else fail("execve NULL argv");
    }

    puts("--- test_execve done ---");
    exit(failed ? 1 : 0);
}