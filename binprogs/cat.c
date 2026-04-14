#include "../apps/cervus_user.h"

static int looks_binary(const char *buf, ssize_t n) {
    int nonprint = 0;
    for (ssize_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)buf[i];
        if (c == 0x00) return 1;
        if (c < 0x08 || (c > 0x0d && c < 0x20 && c != 0x1b))
            nonprint++;
    }
    return (n > 0 && nonprint * 100 / n > 30);
}

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

        cervus_stat_t st;
        if (stat(resolved, &st) == 0 && st.st_type == DT_DIR) {
            fprintf(2, "cat: %s: Is a directory\n", argv[i]);
            rc = 1; continue;
        }

        int fd = open(resolved, O_RDONLY, 0);
        if (fd < 0) {
            fprintf(2, "cat: cannot open: %s\n", argv[i]);
            rc = 1; continue;
        }

        char buf[512];
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n < 0) {
            fprintf(2, "cat: read error: %s\n", argv[i]);
            close(fd);
            rc = 1; continue;
        }
        if (n == 0) {
            close(fd);
            continue;
        }

        if (looks_binary(buf, n)) {
            fprintf(2, "cat: %s: binary file, skipping\n", argv[i]);
            close(fd);
            rc = 1; continue;
        }
        write(1, buf, (size_t)n);

        while ((n = read(fd, buf, sizeof(buf))) > 0) {
            if (looks_binary(buf, n)) {
                fprintf(2, "cat: %s: binary data in file, stopping\n", argv[i]);
                rc = 1; break;
            }
            write(1, buf, (size_t)n);
        }
        close(fd);
    }
    exit(rc);
}