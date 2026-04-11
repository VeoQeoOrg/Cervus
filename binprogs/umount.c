#include "cervus_user.h"

#define SYS_DISK_UMOUNT 531

static inline int disk_umount(const char *path) {
    return (int)syscall1(SYS_DISK_UMOUNT, path);
}

CERVUS_MAIN(main) {
    (void)argc; (void)argv;

    const char *path = NULL;
    for (int i = 0; i < argc; i++) {
        if (is_shell_flag(argv[i])) continue;
        if (i > 0 && !path) {
            int ai = 0;
            for (int j = 0; j <= i; j++)
                if (!is_shell_flag(argv[j])) ai++;
            if (ai >= 2) { path = argv[i]; break; }
        }
    }

    if (!path) {
        ws("Usage: umount <mountpoint>\n");
        exit(1);
    }

    int r = disk_umount(path);
    if (r < 0) {
        wse("umount: failed (error ");
        print_i64(r);
        wse(")\n");
        exit(1);
    }

    ws("Unmounted ");
    ws(path);
    ws("\n");
}