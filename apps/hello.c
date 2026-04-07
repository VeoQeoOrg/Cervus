#include "../apps/cervus_user.h"

static void print_ok  (const char *s) { printf("  [OK]  %s\n", s); }
static void print_fail(const char *s) { printf("  [FAIL] %s\n", s); }

static int run_test(const char *path, const char *argv[]) {
    pid_t child = fork();
    if (child < 0) { printf("  fork failed (%d)\n", (int)child); return -1; }
    if (child == 0) {
        execve(path, argv, NULL);
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
    printf("  PID:  %d\n",  (int)getpid());
    printf("  PPID: %d\n",  (int)getppid());
    printf("  UID:  %d\n",  (int)getuid());
    printf("  GID:  %d\n",  (int)getgid());
    printf("  CAPS: %llx\n", (unsigned long long)cap_get());

    static const struct { const char *name; const char *path; } tests[] = {
        { "process test", "/apps/test_process" },
        { "file I/O test", "/apps/test_files"  },
        { "pipe test",     "/apps/test_pipe"    },
        { "execve test",   "/apps/test_execve"  },
        { "memory test",   "/apps/test_mem"     },
    };
    static const int ntests = 5;

    for (int t = 0; t < ntests; t++) {
        printf("\n=== %s (%s) ===\n", tests[t].name, tests[t].path);
        const char *argv[] = { tests[t].path, NULL };
        rc = run_test(tests[t].path, argv);
        if (rc == 0) print_ok(tests[t].name);
        else { printf("  exit code = %d\n", rc); print_fail(tests[t].name); }
    }

    puts("\n========================================");
    puts("  All tests finished.");
    puts("========================================");
}

CERVUS_MAIN(hello_main) {
    (void)argc; (void)argv;
    run_all_tests();
    exit(0);
}