#include "../apps/cervus_user.h"

CERVUS_MAIN(cat_main) {
    const char *cwd = get_cwd_flag(argc, argv);
    if (argc < 2 || (argc == 2 && argv[1][0] == '-' && argv[1][1] == '-')) {
        ws(C_RED "usage: cat <file>" C_RESET "\n");
        exit(1);
    }
    int rc = 0;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] == '-')
            continue;
        char resolved[512];
        resolve_path(cwd, argv[i], resolved, sizeof(resolved));
        int fd = open(resolved, O_RDONLY, 0);
        if (fd < 0) {
            ws(C_RED "cat: cannot open: " C_RESET);
            ws(argv[i]);
            wn();
            rc = 1;
            continue;
        }
        char buf[512];
        ssize_t n;
        while ((n = read(fd, buf, sizeof(buf))) > 0)
            write(1, buf, (size_t)n);
        close(fd);
    }
    exit(rc);
}