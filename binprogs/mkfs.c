#include "cervus_user.h"

#define SYS_DISK_FORMAT 532

static inline int disk_format(const char *devname, const char *label) {
    return (int)syscall2(SYS_DISK_FORMAT, devname, label);
}

CERVUS_MAIN(main) {
    (void)argc; (void)argv;

    const char *devname = NULL;
    const char *label   = NULL;
    int ai = 0;
    for (int i = 0; i < argc; i++) {
        if (is_shell_flag(argv[i])) continue;
        if (ai == 1) devname = argv[i];
        if (ai == 2) label   = argv[i];
        ai++;
    }

    if (!devname) {
        ws("Usage: mkfs <device> [label]\n");
        ws("  e.g: mkfs hda mydisk\n");
        ws("\nFormats /dev/<device> with CervusFS.\n");
        ws("WARNING: all data on device will be lost!\n");
        exit(1);
    }

    ws("Formatting ");
    ws(devname);
    ws("...\n");

    int r = disk_format(devname, label ? label : devname);
    if (r < 0) {
        wse("mkfs: format failed (error ");
        print_i64(r);
        wse(")\n");
        exit(1);
    }

    ws("Done. CervusFS created on ");
    ws(devname);
    ws("\n");
}