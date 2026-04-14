#include "cervus_user.h"

#define SYS_DISK_MOUNT  530

static inline int disk_mount(const char *devname, const char *path) {
    return (int)syscall2(SYS_DISK_MOUNT, devname, path);
}

CERVUS_MAIN(main) {
    (void)argc; (void)argv;

    int real_argc = 0;
    for (int i = 0; i < argc; i++) {
        if (!is_shell_flag(argv[i])) real_argc++;
    }

    if (real_argc < 3) {
        ws("Usage: mount <device> <mountpoint>\n");
        ws("  e.g: mount hda /mnt/disk\n");
        ws("\nMounts /dev/<device> with Ext2 at <mountpoint>.\n");
        exit(1);
    }

    const char *devname = NULL;
    const char *path    = NULL;
    int ai = 0;
    for (int i = 0; i < argc; i++) {
        if (is_shell_flag(argv[i])) continue;
        if (ai == 1) devname = argv[i];
        if (ai == 2) path    = argv[i];
        ai++;
    }

    if (!devname || !path) {
        wse("mount: missing arguments\n");
        exit(1);
    }

    int r = disk_mount(devname, path);
    if (r < 0) {
        wse("mount: failed to mount ");
        wse(devname);
        wse(" at ");
        wse(path);
        wse(" (error ");
        print_i64(r);
        wse(")\n");
        exit(1);
    }

    ws("Mounted ");
    ws(devname);
    ws(" at ");
    ws(path);
    ws("\n");
}