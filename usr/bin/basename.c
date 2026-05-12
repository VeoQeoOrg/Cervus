#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <cervus_util.h>

int main(int argc, char **argv)
{
    const char *path = NULL;
    const char *suffix = NULL;
    int seen = 0;
    for (int i = 1; i < argc; i++) {
        if (is_shell_flag(argv[i])) continue;
        if (seen == 0) { path = argv[i]; seen++; }
        else if (seen == 1) { suffix = argv[i]; seen++; }
    }
    if (!path) {
        fputs(C_RED "usage: basename <path> [<suffix>]" C_RESET "\n", stderr);
        return 1;
    }

    char buf[512];
    strncpy(buf, path, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    int blen = (int)strlen(buf);
    while (blen > 1 && buf[blen - 1] == '/') buf[--blen] = '\0';

    const char *b = buf;
    for (const char *q = buf; *q; q++) if (*q == '/') b = q + 1;

    if (suffix && *suffix) {
        int bl = (int)strlen(b);
        int sl = (int)strlen(suffix);
        if (sl < bl && strcmp(b + bl - sl, suffix) == 0) {
            char out[256];
            int n = bl - sl;
            if (n > (int)sizeof(out) - 1) n = sizeof(out) - 1;
            memcpy(out, b, n);
            out[n] = '\0';
            fputs(out, stdout);
            putchar('\n');
            return 0;
        }
    }
    fputs(b, stdout);
    putchar('\n');
    return 0;
}
