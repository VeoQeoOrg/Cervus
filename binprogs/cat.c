#include "../apps/cervus_user.h"

CERVUS_MAIN(cat_main) {
    if (argc < 2) {
        ws(C_RED "usage: cat <file>" C_RESET "\n");
        exit(1);
    }
    int rc = 0;
    for (int i = 1; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY, 0);
        if (fd < 0) {
            ws(C_RED "cat: cannot open: " C_RESET); ws(argv[i]); wn();
            rc = 1; continue;
        }
        char buf[512]; ssize_t n;
        while ((n = read(fd, buf, sizeof(buf))) > 0)
            write(1, buf, (size_t)n);
        close(fd);
        if (n == -EINTR) { ws("^C\n"); rc = 1; }
    }
    exit(rc);
}