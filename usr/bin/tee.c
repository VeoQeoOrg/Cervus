#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <cervus_util.h>

static const char USAGE[] =
    "Usage: tee [-ai] [file ...]\nCopy standard input to each FILE and standard output.\n\n  -a   append to FILEs instead of overwriting\n  -i   ignored (kept for portability)\n";

static void usage(void) { fputs(USAGE, stderr); }

int main(int argc, char **argv)
{
    if (cervus_check_help_version(argc, argv, USAGE, "tee")) return 0;
    const char *cwd = get_cwd_flag(argc, argv);
    argc = cervus_filter_args(argc, argv);

    int append = 0;
    int opt;
    while ((opt = getopt(argc, argv, "ai")) != -1) {
        switch (opt) {
            case 'a': append = 1; break;
            case 'i': break;
            default: usage(); return 1;
        }
    }

    int fds[32];
    int nfd = 0;
    int rc = 0;
    for (int i = optind; i < argc && nfd < 32; i++) {
        char resolved[512];
        resolve_path(cwd, argv[i], resolved, sizeof(resolved));
        int fl = O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC);
        int fd = open(resolved, fl, 0644);
        if (fd < 0) {
            fprintf(stderr, "tee: cannot open '%s'\n", argv[i]);
            rc = 1;
            continue;
        }
        fds[nfd++] = fd;
    }

    char buf[4096];
    ssize_t n;
    while ((n = read(0, buf, sizeof(buf))) > 0) {
        write(1, buf, (size_t)n);
        for (int i = 0; i < nfd; i++) {
            ssize_t off = 0;
            while (off < n) {
                ssize_t w = write(fds[i], buf + off, (size_t)(n - off));
                if (w <= 0) break;
                off += w;
            }
        }
    }
    for (int i = 0; i < nfd; i++) close(fds[i]);
    return rc;
}
