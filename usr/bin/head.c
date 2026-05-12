#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <cervus_util.h>

static int do_head(const char *path, int nlines)
{
    int fd;
    if (path) {
        fd = open(path, O_RDONLY);
        if (fd < 0) {
            fprintf(stderr, "head: cannot open '%s'\n", path);
            return 1;
        }
    } else {
        fd = 0;
    }
    char buf[4096];
    int count = 0;
    int done = 0;
    while (!done) {
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n <= 0) break;
        ssize_t start = 0;
        for (ssize_t i = 0; i < n; i++) {
            if (buf[i] == '\n') {
                count++;
                if (count >= nlines) {
                    write(1, buf + start, (size_t)(i + 1 - start));
                    done = 1;
                    break;
                }
            }
        }
        if (!done) write(1, buf, (size_t)n);
    }
    if (path) close(fd);
    return 0;
}

int main(int argc, char **argv)
{
    const char *cwd = get_cwd_flag(argc, argv);
    int nlines = 10;
    const char *files[64];
    int nf = 0;
    for (int i = 1; i < argc; i++) {
        if (is_shell_flag(argv[i])) continue;
        if (argv[i][0] == '-' && argv[i][1] == 'n') {
            const char *val = argv[i] + 2;
            if (!*val && i + 1 < argc) val = argv[++i];
            nlines = atoi(val);
            if (nlines < 0) nlines = 0;
            continue;
        }
        if (nf < 64) files[nf++] = argv[i];
    }

    if (nf == 0) return do_head(NULL, nlines);

    int rc = 0;
    for (int i = 0; i < nf; i++) {
        char resolved[512];
        resolve_path(cwd, files[i], resolved, sizeof(resolved));
        if (nf > 1) {
            fputs("==> ", stdout);
            fputs(files[i], stdout);
            fputs(" <==\n", stdout);
        }
        if (do_head(resolved, nlines) != 0) rc = 1;
        if (nf > 1 && i < nf - 1) putchar('\n');
    }
    return rc;
}
