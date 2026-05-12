#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <cervus_util.h>

static int try_path(const char *dir, const char *name, char *out, size_t sz)
{
    char p[512];
    snprintf(p, sizeof(p), "%s/%s", dir, name);
    struct stat st;
    if (stat(p, &st) == 0 && st.st_type != 1) {
        strncpy(out, p, sz - 1);
        out[sz - 1] = '\0';
        return 1;
    }
    char pe[512];
    snprintf(pe, sizeof(pe), "%s/%s.elf", dir, name);
    if (stat(pe, &st) == 0 && st.st_type != 1) {
        strncpy(out, pe, sz - 1);
        out[sz - 1] = '\0';
        return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    const char *names[32];
    int nn = 0;
    for (int i = 1; i < argc && nn < 32; i++) {
        if (is_shell_flag(argv[i])) continue;
        names[nn++] = argv[i];
    }
    if (nn == 0) {
        fputs(C_RED "usage: which <name>..." C_RESET "\n", stderr);
        return 1;
    }

    const char *pathvar = getenv_argv(argc, argv, "PATH", "/bin:/apps:/usr/bin");
    int rc = 0;
    for (int i = 0; i < nn; i++) {
        char tmp[256];
        strncpy(tmp, pathvar, sizeof(tmp) - 1);
        tmp[sizeof(tmp) - 1] = '\0';
        char *p = tmp;
        int found = 0;
        while (*p) {
            char *seg = p;
            while (*p && *p != ':') p++;
            if (*p == ':') *p++ = '\0';
            if (seg[0]) {
                char hit[512];
                if (try_path(seg, names[i], hit, sizeof(hit))) {
                    fputs(hit, stdout);
                    putchar('\n');
                    found = 1;
                    break;
                }
            }
        }
        if (!found) {
            fprintf(stderr, "which: %s: not found\n", names[i]);
            rc = 1;
        }
    }
    return rc;
}
