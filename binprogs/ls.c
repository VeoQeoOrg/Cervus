#include "../apps/cervus_user.h"

CERVUS_MAIN(ls_main) {
    const char *path = (argc > 1) ? argv[1] : "/";
    int fd = open(path, O_RDONLY | O_DIRECTORY, 0);
    if (fd < 0) {
        ws(C_RED "ls: cannot open: " C_RESET); ws(path); wn();
        exit(1);
    }
    wn();
    cervus_dirent_t de;
    int cnt = 0;
    while (readdir(fd, &de) == 0) {
        ws("  ");
        if (de.d_type == DT_DIR)       ws(C_BLUE);
        else if (de.d_type == DT_CHR)  ws(C_YELLOW);
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