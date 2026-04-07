#include "../apps/cervus_user.h"

CERVUS_MAIN(cat_main) {
    const char *cwd = get_cwd_flag(argc, argv);
    int had_file = 0;
    for (int i = 1; i < argc; i++) {
        if (is_shell_flag(argv[i])) continue;
        had_file = 1;
        break;
    }
    if (!had_file) { ws(C_RED "usage: cat <file>" C_RESET "\n"); exit(1); }

    int rc = 0;
    for (int i = 1; i < argc; i++) {
        if (is_shell_flag(argv[i])) continue;
        char resolved[512];
        resolve_path(cwd, argv[i], resolved, sizeof(resolved));
        int fd = open(resolved, O_RDONLY, 0);
        if (fd < 0) {
            fprintf(2, "cat: cannot open: %s\n", argv[i]);
            rc = 1; continue;
        }
        char buf[512];
        ssize_t n;
        while ((n = read(fd, buf, sizeof(buf))) > 0)
            write(1, buf, (size_t)n);
        close(fd);
    }
    exit(rc);
}