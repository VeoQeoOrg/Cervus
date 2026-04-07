#include "../apps/cervus_user.h"

CERVUS_MAIN(ls_main) {
    const char *cwd = get_cwd_flag(argc, argv);
    const char *path = cwd;
    for (int i = 1; i < argc; i++) {
        if (is_shell_flag(argv[i])) continue;
        path = argv[i]; break;
    }

    char resolved[512];
    resolve_path(cwd, path, resolved, sizeof(resolved));

    int fd = open(resolved, O_RDONLY | O_DIRECTORY, 0);
    if (fd < 0) {
        fprintf(2, "ls: cannot open: %s\n", path);
        exit(1);
    }

    wn();
    cervus_dirent_t de;
    int cnt = 0;
    while (readdir(fd, &de) == 0) {
        ws("  ");
        if      (de.d_type == DT_DIR) ws(C_BLUE);
        else if (de.d_type == DT_CHR) ws(C_YELLOW);
        ws(de.d_name);
        ws(C_RESET);
        if (de.d_type == DT_DIR) wc('/');
        wn();
        cnt++;
    }
    if (!cnt) ws("  " C_GRAY "(empty)" C_RESET "\n");
    wn();
    close(fd);
    exit(0);
}