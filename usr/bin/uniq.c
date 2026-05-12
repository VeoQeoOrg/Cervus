#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <cervus_util.h>

int main(int argc, char **argv)
{
    const char *cwd = get_cwd_flag(argc, argv);
    int count_mode = 0, duplicates_only = 0, uniques_only = 0;
    const char *path = NULL;
    for (int i = 1; i < argc; i++) {
        if (is_shell_flag(argv[i])) continue;
        if (strcmp(argv[i], "-c") == 0) { count_mode = 1; continue; }
        if (strcmp(argv[i], "-d") == 0) { duplicates_only = 1; continue; }
        if (strcmp(argv[i], "-u") == 0) { uniques_only = 1; continue; }
        path = argv[i];
    }

    int fd = 0;
    if (path) {
        char resolved[512];
        resolve_path(cwd, path, resolved, sizeof(resolved));
        fd = open(resolved, O_RDONLY);
        if (fd < 0) { fprintf(stderr, "uniq: cannot open '%s'\n", path); return 1; }
    }

    char prev[4096]; prev[0] = '\0';
    char cur[4096];  int clen = 0;
    int prev_set = 0;
    int prev_count = 0;
    char buf[4096];
    ssize_t n;

    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        for (ssize_t i = 0; i < n; i++) {
            char c = buf[i];
            if (c == '\n' || clen + 1 >= (int)sizeof(cur)) {
                cur[clen] = '\0';
                if (!prev_set || strcmp(cur, prev) != 0) {
                    if (prev_set) {
                        int show = 1;
                        if (duplicates_only && prev_count < 2) show = 0;
                        if (uniques_only    && prev_count > 1) show = 0;
                        if (show) {
                            if (count_mode) printf("%7d ", prev_count);
                            fputs(prev, stdout);
                            putchar('\n');
                        }
                    }
                    strncpy(prev, cur, sizeof(prev) - 1);
                    prev[sizeof(prev) - 1] = '\0';
                    prev_set = 1;
                    prev_count = 1;
                } else {
                    prev_count++;
                }
                clen = 0;
                if (c != '\n') cur[clen++] = c;
            } else {
                cur[clen++] = c;
            }
        }
    }
    if (clen > 0) {
        cur[clen] = '\0';
        if (!prev_set || strcmp(cur, prev) != 0) {
            if (prev_set) {
                int show = 1;
                if (duplicates_only && prev_count < 2) show = 0;
                if (uniques_only    && prev_count > 1) show = 0;
                if (show) {
                    if (count_mode) printf("%7d ", prev_count);
                    fputs(prev, stdout);
                    putchar('\n');
                }
            }
            strncpy(prev, cur, sizeof(prev) - 1);
            prev[sizeof(prev) - 1] = '\0';
            prev_set = 1;
            prev_count = 1;
        } else {
            prev_count++;
        }
    }
    if (prev_set) {
        int show = 1;
        if (duplicates_only && prev_count < 2) show = 0;
        if (uniques_only    && prev_count > 1) show = 0;
        if (show) {
            if (count_mode) printf("%7d ", prev_count);
            fputs(prev, stdout);
            putchar('\n');
        }
    }
    if (fd != 0) close(fd);
    return 0;
}
