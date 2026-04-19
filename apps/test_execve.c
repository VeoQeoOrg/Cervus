#include "../apps/cervus_user.h"

static int failed = 0;
static void ok(const char *s)   { printf("  [OK]  %s\n", s); }
static void fail(const char *s) { printf("  [FAIL] %s\n", s); failed = 1; }

static int file_exists(const char *path) {
    cervus_stat_t st;
    return stat(path, &st) == 0;
}

static const char *resolve_app(const char *name, char *buf, size_t bufsz) {
    const char *prefixes[] = { "/mnt/apps/", "/apps/", "/mnt/bin/", "/bin/", NULL };
    for (int i = 0; prefixes[i]; i++) {
        size_t pl = 0; while (prefixes[i][pl]) pl++;
        size_t nl = 0; while (name[nl]) nl++;
        if (pl + nl + 1 > bufsz) continue;
        size_t o = 0;
        for (size_t k = 0; k < pl; k++) buf[o++] = prefixes[i][k];
        for (size_t k = 0; k < nl; k++) buf[o++] = name[k];
        buf[o] = '\0';
        if (file_exists(buf)) return buf;
    }
    return NULL;
}

CERVUS_MAIN(test_execve_main) {
    (void)argc; (void)argv;
    puts("--- test_execve ---");

    char target[256];
    if (!resolve_app("execve_target", target, sizeof(target))) {
        printf("  [SKIP] execve_target not found\n");
        puts("--- test_execve done ---");
        exit(0);
    }

    {
        const char *cargv[] = { target, "hello", "from", "execve", NULL };
        pid_t child = fork();
        if (child < 0) { fail("fork"); exit(1); }
        if (child == 0) {
            execve(target, cargv, NULL);
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
            const char *cargv[] = { "/no/such/binary", NULL };
            int r = execve("/no/such/binary", cargv, NULL);
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
            execve(target, NULL, NULL);
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