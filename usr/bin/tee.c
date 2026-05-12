#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <cervus_util.h>

int main(int argc, char **argv)
{
    const char *cwd = get_cwd_flag(argc, argv);
    int append = 0;
    int fds[32];
    int nfd = 0;

    for (int i = 1; i < argc; i++) {
        if (is_shell_flag(argv[i])) continue;
        if (strcmp(argv[i], "-a") == 0) { append = 1; continue; }
        if (nfd >= 32) continue;
        char resolved[512];
        resolve_path(cwd, argv[i], resolved, sizeof(resolved));
        int fl = O_WRONLY | O_CREAT | (append ? 0 : O_TRUNC);
        int fd = open(resolved, fl, 0644);
        if (fd < 0) {
            fprintf(stderr, "tee: cannot open '%s'\n", argv[i]);
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
    return 0;
}
