#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

static int g_fail = 0;

static void report_ok(const char *what)
{
    printf("  \x1b[1;32m[ OK ]\x1b[0m %s\n", what);
}
static void report_fail(const char *what)
{
    printf("  \x1b[1;31m[FAIL]\x1b[0m %s\n", what);
    g_fail++;
}
static void report_info(const char *what)
{
    printf("  \x1b[1;36m[info]\x1b[0m %s\n", what);
}

static int exists_any(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}
static int exists_dir(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && st.st_type == 1;
}

static const char *find_sysroot_prefix(void)
{
    if (exists_any("/usr/share/cervus/README.sysroot"))      return "";
    if (exists_any("/mnt/usr/share/cervus/README.sysroot"))  return "/mnt";
    return NULL;
}

static void check_path(const char *prefix, const char *rel, int must_be_dir)
{
    char buf[512];
    snprintf(buf, sizeof(buf), "%s%s", prefix, rel);
    int ok = must_be_dir ? exists_dir(buf) : exists_any(buf);
    if (ok) report_ok(buf);
    else    report_fail(buf);
}

static int path_contains(const char *hay, const char *needle)
{
    if (!hay || !needle) return 0;
    size_t nl = strlen(needle);
    for (const char *p = hay; *p; p++) {
        if (strncmp(p, needle, nl) == 0) {
            char end = p[nl];
            if (end == '\0' || end == ':') return 1;
        }
    }
    return 0;
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    printf("\n\x1b[1;36mCervus sysroot test\x1b[0m\n");
    printf("  \x1b[90m------------------------------------------\x1b[0m\n");

    const char *prefix = find_sysroot_prefix();
    if (!prefix) {
        printf("\x1b[1;31m  No sysroot detected.\x1b[0m\n");
        printf("  Looked for:\n");
        printf("    /usr/share/cervus/README.sysroot\n");
        printf("    /mnt/usr/share/cervus/README.sysroot\n\n");
        return 1;
    }

    report_info(prefix[0]
                ? "sysroot detected under /mnt (installed)"
                : "sysroot detected under / (initramfs/live)");

    printf("\n  \x1b[1mLayout\x1b[0m\n");
    check_path(prefix, "/usr",              1);
    check_path(prefix, "/usr/bin",          1);
    check_path(prefix, "/usr/lib",          1);
    check_path(prefix, "/usr/include",      1);
    check_path(prefix, "/usr/include/sys",  1);
    check_path(prefix, "/usr/share",        1);
    check_path(prefix, "/usr/share/cervus", 1);

    printf("\n  \x1b[1mHeaders\x1b[0m\n");
    static const char *hdrs[] = {
        "/usr/include/stdio.h",
        "/usr/include/stdlib.h",
        "/usr/include/string.h",
        "/usr/include/unistd.h",
        "/usr/include/fcntl.h",
        "/usr/include/ctype.h",
        "/usr/include/errno.h",
        "/usr/include/assert.h",
        "/usr/include/limits.h",
        "/usr/include/dirent.h",
        "/usr/include/stddef.h",
        "/usr/include/stdint.h",
        "/usr/include/stdbool.h",
        "/usr/include/stdarg.h",
        "/usr/include/sys/syscall.h",
        "/usr/include/sys/types.h",
        "/usr/include/sys/stat.h",
        "/usr/include/sys/cervus.h",
        NULL
    };
    for (int i = 0; hdrs[i]; i++) check_path(prefix, hdrs[i], 0);

    printf("\n  \x1b[1mLibraries\x1b[0m\n");
    check_path(prefix, "/usr/lib/libcervus.a", 0);
    check_path(prefix, "/usr/lib/crt0.o",      0);

    printf("\n  \x1b[1mForbidden (must be absent: clean split)\x1b[0m\n");
    {
        char buf[512];
        const char *forbidden[] = {
            "/usr/include/cerlib.h",
            "/usr/include/cervus.h",
            "/usr/include/cervus_user.h",
            NULL
        };
        for (int i = 0; forbidden[i]; i++) {
            snprintf(buf, sizeof(buf), "%s%s", prefix, forbidden[i]);
            if (exists_any(buf)) {
                printf("  \x1b[1;31m[BAD!]\x1b[0m %s still present (should be deleted)\n", buf);
                g_fail++;
            } else {
                printf("  \x1b[1;32m[gone]\x1b[0m %s (good)\n", buf);
            }
        }
    }

    printf("\n  \x1b[1mDocumentation\x1b[0m\n");
    check_path(prefix, "/usr/share/cervus/README.sysroot", 0);

    printf("\n  \x1b[1mPATH\x1b[0m\n");
    const char *path = getenv("PATH");
    if (!path) {
        report_info("no PATH in env (run from shell to see the real PATH)");
    } else {
        char line[600];
        snprintf(line, sizeof(line), "PATH = %s", path);
        report_info(line);
        const char *want = prefix[0] ? "/mnt/usr/bin" : "/usr/bin";
        if (path_contains(path, want))
            report_ok(want);
        else
            report_fail(want);
    }

    printf("\n  \x1b[90m------------------------------------------\x1b[0m\n");
    if (g_fail == 0) {
        printf("  \x1b[1;32mSysroot looks good.\x1b[0m\n\n");
        return 0;
    }
    printf("  \x1b[1;31m%d check(s) failed.\x1b[0m\n\n", g_fail);
    return 1;
}
