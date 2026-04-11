#include "../apps/cervus_user.h"

#define SYS_RENAME 537

static inline int sys_rename(const char *oldpath, const char *newpath) {
    return (int)syscall2(SYS_RENAME, oldpath, newpath);
}

CERVUS_MAIN(mv_main) {
    const char *cwd_str = get_cwd_flag(argc, argv);

    const char *src = NULL, *dst = NULL;
    for (int i = 1; i < argc; i++) {
        if (is_shell_flag(argv[i])) continue;
        if (!src) src = argv[i];
        else if (!dst) dst = argv[i];
    }

    if (!src || !dst) {
        ws("Usage: mv <source> <destination>\n");
        exit(1);
    }

    char srcpath[512], dstpath[512];
    resolve_path(cwd_str, src, srcpath, sizeof(srcpath));
    resolve_path(cwd_str, dst, dstpath, sizeof(dstpath));

    int r = sys_rename(srcpath, dstpath);
    if (r < 0) {
        wse("mv: cannot move '");
        wse(src);
        wse("' to '");
        wse(dst);
        wse("' (error ");
        print_i64(r);
        wse(")\n");
        exit(1);
    }
}