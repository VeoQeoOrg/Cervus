#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/cervus.h>

static int failed = 0;
static void ok(const char *s)   { printf("  [OK]  %s\n", s); }
static void fail(const char *s) { printf("  [FAIL] %s\n", s); failed = 1; }

static int file_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

static const char *resolve_app(const char *name, char *buf, size_t bufsz)
{
    const char *prefixes[] = { "/mnt/apps/", "/apps/", "/mnt/bin/", "/bin/", NULL };
    for (int i = 0; prefixes[i]; i++) {
        size_t pl = strlen(prefixes[i]);
        size_t nl = strlen(name);
        if (pl + nl + 1 > bufsz) continue;
        memcpy(buf, prefixes[i], pl);
        memcpy(buf + pl, name, nl + 1);
        if (file_exists(buf)) return buf;
    }
    return NULL;
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    puts("--- test_execve ---");

    char target[256];
    if (!resolve_app("execve_target", target, sizeof(target))) {
        printf("  [SKIP] execve_target not found\n");
        puts("--- test_execve done ---");
        return 0;
    }

    {
        char *const cargv[] = { target, (char *)"hello", (char *)"from", (char *)"execve", NULL };
        pid_t child = fork();
        if (child < 0) { fail("fork"); return 1; }
        if (child == 0) {
            execve(target, cargv, NULL);
            printf("  [FATAL] execve failed\n");
            return 127;
        }
        int status = 0;
        waitpid(child, &status, 0);
        if (WIFEXITED(status) && WEXITSTATUS(status) == 99) ok("execve target exits 99");
        else {
            printf("  exit status = %d\n", WEXITSTATUS(status));
            fail("execve exit code");
        }
    }

    {
        pid_t child = fork();
        if (child < 0) { fail("fork"); return 1; }
        if (child == 0) {
            char *const cargv[] = { (char *)"/no/such/binary", NULL };
            int r = execve("/no/such/binary", cargv, NULL);
            return (r < 0) ? 0 : 1;
        }
        int status = 0;
        waitpid(child, &status, 0);
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) ok("execve nonexistent returns error");
        else fail("execve nonexistent should return error");
    }

    {
        pid_t child = fork();
        if (child < 0) { fail("fork"); return 1; }
        if (child == 0) {
            execve(target, NULL, NULL);
            return 127;
        }
        int status = 0;
        waitpid(child, &status, 0);
        if (WIFEXITED(status) && WEXITSTATUS(status) == 99) ok("execve with NULL argv");
        else fail("execve NULL argv");
    }

    puts("--- test_execve done ---");
    return failed ? 1 : 0;
}
