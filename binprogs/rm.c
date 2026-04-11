#include "../apps/cervus_user.h"

#define SYS_UNLINK  534
#define SYS_RMDIR   535

static inline int sys_unlink(const char *path) {
    return (int)syscall1(SYS_UNLINK, path);
}

static inline int sys_rmdir(const char *path) {
    return (int)syscall1(SYS_RMDIR, path);
}

CERVUS_MAIN(rm_main) {
    const char *cwd_str = get_cwd_flag(argc, argv);
    int recursive = 0;
    int found = 0;

    for (int i = 1; i < argc; i++) {
        if (is_shell_flag(argv[i])) continue;
        if (strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "-rf") == 0 ||
            strcmp(argv[i], "-R") == 0) {
            recursive = 1;
            continue;
        }

        found++;
        char path[512];
        resolve_path(cwd_str, argv[i], path, sizeof(path));

        cervus_stat_t st;
        if (stat(path, &st) < 0) {
            wse("rm: cannot remove '");
            wse(argv[i]);
            wse("': No such file or directory\n");
            continue;
        }

        int r;
        if (st.st_type == DT_DIR) {
            if (!recursive) {
                wse("rm: '");
                wse(argv[i]);
                wse("' is a directory (use -r)\n");
                continue;
            }
            r = sys_rmdir(path);
        } else {
            r = sys_unlink(path);
        }

        if (r < 0) {
            wse("rm: failed to remove '");
            wse(argv[i]);
            wse("' (error ");
            print_i64(r);
            wse(")\n");
        }
    }

    if (!found) {
        ws("Usage: rm [-r] <file|dir> ...\n");
        exit(1);
    }
}