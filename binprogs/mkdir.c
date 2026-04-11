#include "../apps/cervus_user.h"

#define SYS_MKDIR 536

static inline int sys_mkdir(const char *path, int mode) {
    return (int)syscall2(SYS_MKDIR, path, mode);
}

CERVUS_MAIN(mkdir_main) {
    const char *cwd_str = get_cwd_flag(argc, argv);
    int found = 0;

    for (int i = 1; i < argc; i++) {
        if (is_shell_flag(argv[i])) continue;
        if (argv[i][0] == '-') continue;
        found++;

        char path[512];
        resolve_path(cwd_str, argv[i], path, sizeof(path));

        int r = sys_mkdir(path, 0755);
        if (r < 0) {
            if (r == -EEXIST) {
                wse("mkdir: '");
                wse(argv[i]);
                wse("' already exists\n");
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
        ws("Usage: mkdir <dir> [dir2 ...]\n");
        exit(1);
    }
}