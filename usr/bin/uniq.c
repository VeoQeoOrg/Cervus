#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <cervus_util.h>

static int lines_eq(const char *a, const char *b, int ci)
{
    if (!ci) return strcmp(a, b) == 0;
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
        a++; b++;
    }
    return *a == 0 && *b == 0;
}

static void flush(const char *prev, int prev_count, int count_mode,
                  int dup_only, int uniq_only)
{
    int show = 1;
    if (dup_only && prev_count < 2) show = 0;
    if (uniq_only && prev_count > 1) show = 0;
    if (!show) return;
    if (count_mode) printf("%7d ", prev_count);
    fputs(prev, stdout);
    putchar('\n');
}

static const char USAGE[] =
    "Usage: uniq [-cdiu] [input [output]]\nFilter adjacent matching lines from INPUT.\n\n  -c   prefix lines by number of occurrences\n  -d   only print duplicates\n  -i   ignore case when comparing\n  -u   only print unique lines\n";

static void usage(void) { fputs(USAGE, stderr); }

int main(int argc, char **argv)
{
    if (cervus_check_help_version(argc, argv, USAGE, "uniq")) return 0;
    const char *cwd = get_cwd_flag(argc, argv);
    argc = cervus_filter_args(argc, argv);

    int count_mode = 0, dup_only = 0, uniq_only = 0, ci = 0;
    int opt;
    while ((opt = getopt(argc, argv, "cdiu")) != -1) {
        switch (opt) {
            case 'c': count_mode = 1; break;
            case 'd': dup_only = 1; break;
            case 'i': ci = 1; break;
            case 'u': uniq_only = 1; break;
            default: usage(); return 1;
        }
    }

    int in_fd = 0, out_fd = 1;
    if (optind < argc && strcmp(argv[optind], "-") != 0) {
        char resolved[512];
        resolve_path(cwd, argv[optind], resolved, sizeof(resolved));
        in_fd = open(resolved, O_RDONLY);
        if (in_fd < 0) { fprintf(stderr, "uniq: cannot open '%s'\n", argv[optind]); return 1; }
    }
    if (optind + 1 < argc) {
        char resolved[512];
        resolve_path(cwd, argv[optind + 1], resolved, sizeof(resolved));
        out_fd = open(resolved, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (out_fd < 0) { fprintf(stderr, "uniq: cannot create '%s'\n", argv[optind + 1]); if (in_fd > 0) close(in_fd); return 1; }
        dup2(out_fd, 1);
        close(out_fd);
    }

    char prev[4096]; prev[0] = '\0';
    char cur[4096]; int clen = 0;
    int prev_set = 0, prev_count = 0;
    char buf[4096]; ssize_t n;

    while ((n = read(in_fd, buf, sizeof(buf))) > 0) {
        for (ssize_t i = 0; i < n; i++) {
            char c = buf[i];
            if (c == '\n') {
                cur[clen] = '\0';
                if (!prev_set || !lines_eq(cur, prev, ci)) {
                    if (prev_set) flush(prev, prev_count, count_mode, dup_only, uniq_only);
                    strncpy(prev, cur, sizeof(prev) - 1);
                    prev[sizeof(prev) - 1] = '\0';
                    prev_set = 1; prev_count = 1;
                } else prev_count++;
                clen = 0;
            } else if (clen + 1 < (int)sizeof(cur)) {
                cur[clen++] = c;
            }
        }
    }
    if (clen > 0) {
        cur[clen] = '\0';
        if (!prev_set || !lines_eq(cur, prev, ci)) {
            if (prev_set) flush(prev, prev_count, count_mode, dup_only, uniq_only);
            strncpy(prev, cur, sizeof(prev) - 1);
            prev[sizeof(prev) - 1] = '\0';
            prev_set = 1; prev_count = 1;
        } else prev_count++;
    }
    if (prev_set) flush(prev, prev_count, count_mode, dup_only, uniq_only);
    if (in_fd > 0) close(in_fd);
    return 0;
}
