#include "cervus_user.h"

static void print_ok  (const char *s) { printf("  [OK]  %s\n", s); }
static void print_fail(const char *s) { printf("  [FAIL] %s\n", s); }
static void print_skip(const char *s) { printf("  [SKIP] %s\n", s); }

static int run_test(const char *path, const char *argv[]) {
    pid_t child = fork();
    if (child < 0) { printf("  fork failed (%d)\n", (int)child); return -1; }
    if (child == 0) {
        execve(path, argv, 0);
        printf("  [FATAL] execve(%s) failed\n", path);
        exit(127);
    }
    int status = 0;
    waitpid(child, &status, 0);
    return WEXITSTATUS(status);
}

static void run_all_tests(void) {
    int rc;

    puts("========================================");
    puts("  Cervus OS — Userspace Test Suite");
    puts("========================================\n");

    puts("=== Identity ===");
    printf("  PID:  %d\n", (int)getpid());
    printf("  PPID: %d\n", (int)getppid());
    printf("  UID:  %d\n", (int)getuid());
    printf("  GID:  %d\n", (int)getgid());
    printf("  CAPS: %llx\n", cap_get());

    puts("\n=== test_process (/bin/test_process) ===");
    {
        const char *argv[] = { "/bin/test_process", NULL };
        rc = run_test("/bin/test_process", argv);
        if (rc == 0) print_ok("process test");
        else { printf("  exit code = %d\n", rc); print_fail("process test"); }
    }

    puts("\n=== test_files (/bin/test_files) ===");
    {
        const char *argv[] = { "/bin/test_files", NULL };
        rc = run_test("/bin/test_files", argv);
        if (rc == 0) print_ok("file I/O test");
        else { printf("  exit code = %d\n", rc); print_fail("file I/O test"); }
    }

    puts("\n=== test_pipe (/bin/test_pipe) ===");
    {
        const char *argv[] = { "/bin/test_pipe", NULL };
        rc = run_test("/bin/test_pipe", argv);
        if (rc == 0) print_ok("pipe test");
        else { printf("  exit code = %d\n", rc); print_fail("pipe test"); }
    }

    puts("\n=== test_execve (/bin/test_execve) ===");
    {
        const char *argv[] = { "/bin/test_execve", NULL };
        rc = run_test("/bin/test_execve", argv);
        if (rc == 0) print_ok("execve test");
        else { printf("  exit code = %d\n", rc); print_fail("execve test"); }
    }

    puts("\n=== test_mem (/bin/test_mem) ===");
    {
        const char *argv[] = { "/bin/test_mem", NULL };
        rc = run_test("/bin/test_mem", argv);
        if (rc == 0) print_ok("memory test");
        else { printf("  exit code = %d\n", rc); print_fail("memory test"); }
    }

    puts("\n========================================");
    puts("  All tests finished.");
    puts("========================================");
}

__attribute__((naked)) void _start(void) {
    asm volatile(
        "mov  %%rsp, %%rdi\n\t"
        "and  $-16, %%rsp\n\t"
        "call _start_main\n\t"
        "ud2\n\t"
        ::: "memory"
    );
}

void _start_main(uint64_t *initial_rsp) {
    (void)initial_rsp;
    run_all_tests();
    exit(0);
}