#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <cervus_util.h>

static int cmp_asc(const void *a, const void *b)
{
    const char *sa = *(const char **)a;
    const char *sb = *(const char **)b;
    return strcmp(sa, sb);
}

static int cmp_desc(const void *a, const void *b)
{
    const char *sa = *(const char **)a;
    const char *sb = *(const char **)b;
    return -strcmp(sa, sb);
}

static int cmp_num_asc(const void *a, const void *b)
{
    const char *sa = *(const char **)a;
    const char *sb = *(const char **)b;
    long ia = atol(sa), ib = atol(sb);
    if (ia < ib) return -1;
    if (ia > ib) return 1;
    return 0;
}

int main(int argc, char **argv)
{
    const char *cwd = get_cwd_flag(argc, argv);
    int reverse = 0, numeric = 0, unique = 0;
    const char *path = NULL;
    for (int i = 1; i < argc; i++) {
        if (is_shell_flag(argv[i])) continue;
        if (strcmp(argv[i], "-r") == 0) { reverse = 1; continue; }
        if (strcmp(argv[i], "-n") == 0) { numeric = 1; continue; }
        if (strcmp(argv[i], "-u") == 0) { unique = 1; continue; }
        path = argv[i];
    }

    int fd = 0;
    if (path) {
        char resolved[512];
        resolve_path(cwd, path, resolved, sizeof(resolved));
        fd = open(resolved, O_RDONLY);
        if (fd < 0) { fprintf(stderr, "sort: cannot open '%s'\n", path); return 1; }
    }

    size_t cap = 16384;
    char *blob = malloc(cap);
    if (!blob) { fputs("sort: out of memory\n", stderr); return 1; }
    size_t total = 0;
    char buf[4096];
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        if (total + n + 1 > cap) {
            while (cap < total + n + 1) cap *= 2;
            char *nb = realloc(blob, cap);
            if (!nb) { free(blob); fputs("sort: oom\n", stderr); if (fd) close(fd); return 1; }
            blob = nb;
        }
        memcpy(blob + total, buf, (size_t)n);
        total += n;
    }
    if (fd != 0) close(fd);
    if (total == 0) { free(blob); return 0; }
    blob[total] = '\0';

    int nlines = 1;
    for (size_t i = 0; i < total; i++) if (blob[i] == '\n') nlines++;
    char **lines = malloc(sizeof(char *) * nlines);
    if (!lines) { free(blob); fputs("sort: oom\n", stderr); return 1; }
    int li = 0;
    char *p = blob;
    lines[li++] = p;
    for (size_t i = 0; i < total; i++) {
        if (blob[i] == '\n') {
            blob[i] = '\0';
            if (i + 1 < total) lines[li++] = blob + i + 1;
        }
    }
    nlines = li;

    int (*cmp)(const void *, const void *);
    if (numeric)      cmp = cmp_num_asc;
    else if (reverse) cmp = cmp_desc;
    else              cmp = cmp_asc;

    qsort(lines, nlines, sizeof(char *), cmp);

    if (numeric && reverse) {
        for (int i = 0, j = nlines - 1; i < j; i++, j--) {
            char *t = lines[i]; lines[i] = lines[j]; lines[j] = t;
        }
    }

    for (int i = 0; i < nlines; i++) {
        if (unique && i > 0 && strcmp(lines[i], lines[i - 1]) == 0) continue;
        fputs(lines[i], stdout);
        putchar('\n');
    }
    free(lines);
    free(blob);
    return 0;
}
