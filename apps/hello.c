#include "../apps/cervus_user.h"

static void print_ok  (const char *s) { printf("  [OK]  %s\n", s); }
static void print_fail(const char *s) { printf("  [FAIL] %s\n", s); }

static int file_exists(const char *path) {
    cervus_stat_t st;
    return stat(path, &st) == 0;
}

static int find_app(const char *name, char *out, size_t outsz) {
    const char *prefixes[] = {
        "/mnt/apps/",
        "/apps/",
        "/mnt/bin/",
        "/bin/",
        NULL
    };
    for (int i = 0; prefixes[i]; i++) {
        size_t pl = 0;
        while (prefixes[i][pl]) pl++;
        size_t nl = 0;
        while (name[nl]) nl++;
        if (pl + nl + 1 > outsz) continue;
        size_t o = 0;
        for (size_t k = 0; k < pl; k++) out[o++] = prefixes[i][k];
        for (size_t k = 0; k < nl; k++) out[o++] = name[k];
        out[o] = '\0';
        if (file_exists(out)) return 1;
    }
    return 0;
}

static int run_test(const char *name) {
    char path[256];
    if (!find_app(name, path, sizeof(path))) {
        printf("  [SKIP] %s not found in /apps or /mnt/apps\n", name);
        return -1;
    }
    pid_t child = fork();
    if (child < 0) { printf("  fork failed (%d)\n", (int)child); return -1; }
    if (child == 0) {
        const char *argv[] = { path, NULL };
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
    puts("  Cervus OS -- Userspace Test Suite");
    puts("========================================\n");

    puts("=== Identity ===");
    printf("  PID:  %d\n",  (int)getpid());
    printf("  PPID: %d\n",  (int)getppid());
    printf("  UID:  %d\n",  (int)getuid());
    printf("  GID:  %d\n",  (int)getgid());
    printf("  CAPS: %llx\n", (unsigned long long)cap_get());

    static const struct { const char *name; const char *bin; } tests[] = {
        { "process test", "test_process" },
        { "file I/O test", "test_files"  },
        { "pipe test",     "test_pipe"    },
        { "execve test",   "test_execve"  },
        { "memory test",   "test_mem"     },
    };
    static const int ntests = 5;

    for (int t = 0; t < ntests; t++) {
        printf("\n=== %s ===\n", tests[t].name);
        rc = run_test(tests[t].bin);
        if (rc == 0) print_ok(tests[t].name);
        else if (rc < 0) print_fail(tests[t].name);
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