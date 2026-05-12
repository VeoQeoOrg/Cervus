#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <cervus_util.h>

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
    free(buf);
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

    if (nf == 0) {
        fputs(C_RED "usage: tail [-n N] <file>" C_RESET "\n", stderr);
        return 1;
    }

    int rc = 0;
    for (int i = 0; i < nf; i++) {
        char resolved[512];
        resolve_path(cwd, files[i], resolved, sizeof(resolved));
        if (nf > 1) {
            fputs("==> ", stdout);
            fputs(files[i], stdout);
            fputs(" <==\n", stdout);
        }
        if (do_tail(resolved, nlines) != 0) rc = 1;
        if (nf > 1 && i < nf - 1) putchar('\n');
    }
    return rc;
}
