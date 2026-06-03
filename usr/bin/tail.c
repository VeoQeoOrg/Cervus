#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <cervus_util.h>

static void tail_print_last(const char *buf, ssize_t n, int nlines)
{
    if (n <= 0 || nlines <= 0) return;
    int lf_count = 0;
    ssize_t i = n - 1;
    if (buf[i] == '\n') i--;
    for (; i >= 0; i--) {
        if (buf[i] == '\n') {
            lf_count++;
            if (lf_count >= nlines) { i++; break; }
        }
    }
    if (i < 0) i = 0;
    write(1, buf + i, (size_t)(n - i));
}

static int do_tail_stdin(int nlines)
{
    size_t cap = 65536;
    char *buf = malloc(cap);
    if (!buf) { fputs("tail: out of memory\n", stderr); return 1; }
    size_t len = 0;
    char chunk[4096];
    ssize_t n;
    while ((n = read(0, chunk, sizeof(chunk))) > 0) {
        size_t take = (size_t)n;
        if (take >= cap) {
            memcpy(buf, chunk + (take - cap), cap);
            len = cap;
            continue;
        }
        if (len + take > cap) {
            size_t shift = (len + take) - cap;
            memmove(buf, buf + shift, len - shift);
            len -= shift;
        }
        memcpy(buf + len, chunk, take);
        len += take;
    }
    tail_print_last(buf, (ssize_t)len, nlines);
    free(buf);
    return 0;
}

static int do_tail(const char *path, int nlines)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "tail: cannot open '%s'\n", path);
        return 1;
    }
    struct stat st;
    if (fstat(fd, &st) != 0) { close(fd); return 1; }
    size_t sz = (size_t)st.st_size;
    if (sz == 0) { close(fd); return 0; }

    size_t cap = 65536;
    if (cap > sz) cap = sz;
    char *buf = malloc(cap + 1);
    if (!buf) { close(fd); fputs("tail: out of memory\n", stderr); return 1; }

    if (sz > cap) lseek(fd, (long)(sz - cap), 0);
    ssize_t n = read(fd, buf, cap);
    close(fd);
    if (n <= 0) { free(buf); return 0; }
    buf[n] = '\0';

    tail_print_last(buf, n, nlines);
    free(buf);
    return 0;
}

static const char USAGE[] =
    "Usage: tail [-n lines] [file ...]\nPrint the last 10 lines of each FILE to standard output.\nWith no FILE, or - as FILE, read standard input.\n\n  -n N    print last N lines (default 10)\n  -N      shorthand for -n N\n";

static void usage(void) { fputs(USAGE, stderr); }

int main(int argc, char **argv)
{
    if (cervus_check_help_version(argc, argv, USAGE, "tail")) return 0;
    int nlines = 10;

    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] >= '0' && argv[i][1] <= '9') {
            nlines = atoi(argv[i] + 1);
            if (nlines < 0) nlines = 0;
            for (int j = i; j + 1 < argc; j++) argv[j] = argv[j + 1];
            argc--; i--;
        }
    }


    int opt;
    while ((opt = getopt(argc, argv, "n:q")) != -1) {
        switch (opt) {
            case 'n': nlines = atoi(optarg); if (nlines < 0) nlines = 0; break;
            case 'q': break;
            default: usage(); return 1;
        }
    }

    int nf = argc - optind;
    if (nf == 0) return do_tail_stdin(nlines);

    int rc = 0;
    for (int i = optind; i < argc; i++) {
        char resolved[512];
        snprintf(resolved, sizeof(resolved), "%s", argv[i]);
        if (nf > 1) {
            fputs("==> ", stdout);
            fputs(argv[i], stdout);
            fputs(" <==\n", stdout);
        }
        if (do_tail(resolved, nlines) != 0) rc = 1;
        if (nf > 1 && i + 1 < argc) putchar('\n');
    }
    return rc;
}
