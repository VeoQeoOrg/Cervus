#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <cervus_util.h>

static int g_numeric, g_reverse, g_fold;

static int sort_cmp(const void *a, const void *b)
{
    const char *sa = *(const char **)a;
    const char *sb = *(const char **)b;
    int r;
    if (g_numeric) {
        long ia = atol(sa), ib = atol(sb);
        r = (ia < ib) ? -1 : (ia > ib) ? 1 : 0;
        if (r == 0) r = strcmp(sa, sb);
    } else if (g_fold) {
        while (*sa && *sb) {
            int ca = tolower((unsigned char)*sa++);
            int cb = tolower((unsigned char)*sb++);
            if (ca != cb) { r = ca - cb; goto done; }
        }
        r = (unsigned char)*sa - (unsigned char)*sb;
    } else {
        r = strcmp(sa, sb);
    }
done:
    return g_reverse ? -r : r;
}

static int load_fd(int fd, char **blob_out, size_t *total_out, size_t cap_in)
{
    size_t cap = cap_in ? cap_in : 16384;
    char *blob = malloc(cap);
    if (!blob) { fputs("sort: out of memory\n", stderr); return -1; }
    size_t total = 0;
    char buf[4096];
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        if (total + (size_t)n + 1 > cap) {
            while (cap < total + (size_t)n + 1) cap *= 2;
            char *nb = realloc(blob, cap);
            if (!nb) { free(blob); fputs("sort: oom\n", stderr); return -1; }
            blob = nb;
        }
        memcpy(blob + total, buf, (size_t)n);
        total += (size_t)n;
    }
    blob[total] = '\0';
    *blob_out = blob;
    *total_out = total;
    return 0;
}

static const char USAGE[] =
    "Usage: sort [-fnru] [file ...]\nWrite sorted concatenation of all FILE(s) to standard output.\n\n  -f   ignore case while sorting\n  -n   numeric sort\n  -r   reverse output\n  -u   output only unique lines\n";

static void usage(void) { fputs(USAGE, stderr); }

int main(int argc, char **argv)
{
    if (cervus_check_help_version(argc, argv, USAGE, "sort")) return 0;
    const char *cwd = get_cwd_flag(argc, argv);
    argc = cervus_filter_args(argc, argv);

    int unique = 0;
    g_numeric = g_reverse = g_fold = 0;
    int opt;
    while ((opt = getopt(argc, argv, "fnru")) != -1) {
        switch (opt) {
            case 'f': g_fold = 1; break;
            case 'n': g_numeric = 1; break;
            case 'r': g_reverse = 1; break;
            case 'u': unique = 1; break;
            default: usage(); return 1;
        }
    }

    char *blob = NULL;
    size_t total = 0;

    if (optind >= argc) {
        if (load_fd(0, &blob, &total, 0) < 0) return 1;
    } else {
        size_t cap = 16384;
        blob = malloc(cap);
        if (!blob) { fputs("sort: oom\n", stderr); return 1; }
        for (int i = optind; i < argc; i++) {
            char resolved[512];
            resolve_path(cwd, argv[i], resolved, sizeof(resolved));
            int fd = open(resolved, O_RDONLY);
            if (fd < 0) { fprintf(stderr, "sort: cannot open '%s'\n", argv[i]); free(blob); return 1; }
            char buf[4096]; ssize_t n;
            while ((n = read(fd, buf, sizeof(buf))) > 0) {
                if (total + (size_t)n + 1 > cap) {
                    while (cap < total + (size_t)n + 1) cap *= 2;
                    char *nb = realloc(blob, cap);
                    if (!nb) { free(blob); close(fd); fputs("sort: oom\n", stderr); return 1; }
                    blob = nb;
                }
                memcpy(blob + total, buf, (size_t)n);
                total += (size_t)n;
            }
            close(fd);
        }
        blob[total] = '\0';
    }

    if (total == 0) { free(blob); return 0; }

    int nlines = 1;
    for (size_t i = 0; i < total; i++) if (blob[i] == '\n') nlines++;
    char **lines = malloc(sizeof(char *) * nlines);
    if (!lines) { free(blob); fputs("sort: oom\n", stderr); return 1; }
    int li = 0;
    lines[li++] = blob;
    for (size_t i = 0; i < total; i++) {
        if (blob[i] == '\n') {
            blob[i] = '\0';
            if (i + 1 < total) lines[li++] = blob + i + 1;
        }
    }
    nlines = li;

    qsort(lines, nlines, sizeof(char *), sort_cmp);

    for (int i = 0; i < nlines; i++) {
        if (unique && i > 0) {
            int eq;
            if (g_fold) {
                const char *a = lines[i - 1], *b = lines[i];
                while (*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b)) { a++; b++; }
                eq = (*a == 0 && *b == 0);
            } else {
                eq = strcmp(lines[i - 1], lines[i]) == 0;
            }
            if (eq) continue;
        }
        fputs(lines[i], stdout);
        putchar('\n');
    }
    free(lines);
    free(blob);
    return 0;
}
