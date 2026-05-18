#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <cervus_util.h>

static int head_lines(int fd, int nlines)
{
    char buf[4096];
    int count = 0;
    while (count < nlines) {
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n <= 0) break;
        ssize_t start = 0;
        ssize_t i = 0;
        for (; i < n && count < nlines; i++) {
            if (buf[i] == '\n') {
                count++;
                if (count >= nlines) {
                    write(1, buf + start, (size_t)(i + 1 - start));
                    return 0;
                }
            }
        }
        write(1, buf + start, (size_t)(i - start));
    }
    return 0;
}

static int head_bytes(int fd, long nbytes)
{
    char buf[4096];
    long left = nbytes;
    while (left > 0) {
        size_t want = (size_t)(left < (long)sizeof(buf) ? left : (long)sizeof(buf));
        ssize_t n = read(fd, buf, want);
        if (n <= 0) break;
        write(1, buf, (size_t)n);
        left -= n;
    }
    return 0;
}

static const char USAGE[] =
    "Usage: head [-n lines] [-c bytes] [file ...]\nPrint the first 10 lines of each FILE to standard output.\nWith no FILE, or - as FILE, read standard input.\n\n  -c N    print first N bytes of each file\n  -n N    print first N lines (default 10)\n  -N      shorthand for -n N\n";

static void usage(void) { fputs(USAGE, stderr); }

int main(int argc, char **argv)
{
    if (cervus_check_help_version(argc, argv, USAGE, "head")) return 0;
    const char *cwd = get_cwd_flag(argc, argv);

    int nlines = 10;
    long nbytes = -1;

    for (int i = 1; i < argc; i++) {
        if (!is_shell_flag(argv[i]) &&
            argv[i][0] == '-' && argv[i][1] >= '0' && argv[i][1] <= '9') {
            nlines = atoi(argv[i] + 1);
            if (nlines < 0) nlines = 0;
            for (int j = i; j + 1 < argc; j++) argv[j] = argv[j + 1];
            argc--; i--;
        }
    }

    argc = cervus_filter_args(argc, argv);

    int opt;
    while ((opt = getopt(argc, argv, "n:c:q")) != -1) {
        switch (opt) {
            case 'n': nlines = atoi(optarg); if (nlines < 0) nlines = 0; nbytes = -1; break;
            case 'c': nbytes = atol(optarg); if (nbytes < 0) nbytes = 0; break;
            case 'q': break;
            default: usage(); return 1;
        }
    }

    int nf = argc - optind;
    int rc = 0;

    if (nf == 0) {
        if (nbytes >= 0) return head_bytes(0, nbytes);
        return head_lines(0, nlines);
    }

    for (int i = optind; i < argc; i++) {
        char resolved[512];
        resolve_path(cwd, argv[i], resolved, sizeof(resolved));
        int fd = open(resolved, O_RDONLY);
        if (fd < 0) { fprintf(stderr, "head: cannot open '%s'\n", argv[i]); rc = 1; continue; }
        if (nf > 1) { fputs("==> ", stdout); fputs(argv[i], stdout); fputs(" <==\n", stdout); }
        if (nbytes >= 0) head_bytes(fd, nbytes);
        else             head_lines(fd, nlines);
        close(fd);
        if (nf > 1 && i + 1 < argc) putchar('\n');
    }
    return rc;
}
