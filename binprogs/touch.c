#include "../apps/cervus_user.h"

CERVUS_MAIN(touch_main) {
    const char *cwd_str = get_cwd_flag(argc, argv);

    int found = 0;
    for (int i = 1; i < argc; i++) {
        if (is_shell_flag(argv[i])) continue;
        found++;

        char path[512];
        resolve_path(cwd_str, argv[i], path, sizeof(path));

        cervus_stat_t st;
        if (stat(path, &st) == 0) {
            if (st.st_type == DT_DIR) {
                wse("touch: cannot touch '");
                wse(argv[i]);
                wse("': Is a directory\n");
            }
            continue;
        }

        int fd = open(path, O_WRONLY | O_CREAT, 0644);
        if (fd < 0) {
            wse("touch: cannot create '");
            wse(argv[i]);
            wse("'\n");
            continue;
        }
        close(fd);
    }

    if (!found) {
        ws("Usage: touch <file> [file2 ...]\n");
        exit(1);
    }
}