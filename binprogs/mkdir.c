#include "../apps/cervus_user.h"

#define SYS_MKDIR 536

static inline int sys_mkdir(const char *path, int mode) {
    return (int)syscall2(SYS_MKDIR, path, mode);
}

static int mkdir_p(const char *path) {
    char tmp[512];
    size_t len = strlen(path);
    if (len == 0 || len >= sizeof(tmp)) return -1;
    for (size_t i = 0; i <= len; i++) tmp[i] = path[i];
    for (size_t i = 1; i <= len; i++) {
        if (tmp[i] == '/' || tmp[i] == '\0') {
            char saved = tmp[i];
            tmp[i] = '\0';
            cervus_stat_t st;
            if (stat(tmp, &st) != 0) {
                int r = sys_mkdir(tmp, 0755);
                if (r < 0 && r != -EEXIST) { tmp[i] = saved; return r; }
            }
            tmp[i] = saved;
        }
    }
    return 0;
}

CERVUS_MAIN(mkdir_main) {
    const char *cwd_str = get_cwd_flag(argc, argv);
    int found = 0;
    int flag_p = 0;

    for (int i = 1; i < argc; i++) {
        if (is_shell_flag(argv[i])) continue;
        if (strcmp(argv[i], "-p") == 0) { flag_p = 1; continue; }
        if (argv[i][0] == '-') continue;
        found++;

        char path[512];
        resolve_path(cwd_str, argv[i], path, sizeof(path));

        int r;
        if (flag_p) {
            r = mkdir_p(path);
        } else {
            r = sys_mkdir(path, 0755);
        }
        if (r < 0) {
            if (r == -EEXIST) {
                if (!flag_p) {
                    wse("mkdir: '");
                    wse(argv[i]);
                    wse("' already exists\n");
                }
            } else {
                wse("mkdir: cannot create '");
                wse(argv[i]);
                wse("' (error ");
                print_i64(r);
                wse(")\n");
            }
        }
    }

    if (!found) {
        ws("Usage: mkdir [-p] <dir> [dir2 ...]\n");
        exit(1);
    }
}