#include "cervus_user.h"

#define SYS_DISK_INFO 533

typedef struct {
    char     name[32];
    uint64_t sectors;
    uint64_t size_bytes;
    char     model[41];
    uint8_t  present;
    uint8_t  _pad[6];
} disk_info_t;

static inline int disk_info(int index, disk_info_t *info) {
    return (int)syscall2(SYS_DISK_INFO, index, info);
}

CERVUS_MAIN(main) {
    (void)argc; (void)argv;

    ws("NAME    SIZE       SECTORS    MODEL\n");
    ws("------  ---------  ---------  --------------------------\n");

    int found = 0;
    for (int i = 0; i < 8; i++) {
        disk_info_t info;
        memset(&info, 0, sizeof(info));
        int r = disk_info(i, &info);
        if (r < 0) break;
        if (!info.present) continue;
        found++;

        ws(info.name);
        int nlen = (int)strlen(info.name);
        for (int p = nlen; p < 8; p++) wc(' ');

        uint64_t mb = info.size_bytes / (1024 * 1024);
        if (mb >= 1024) {
            print_u64(mb / 1024);
            ws(".");
            print_u64((mb % 1024) * 10 / 1024);
            ws(" GB");
        } else {
            print_u64(mb);
            ws(" MB");
        }

        char szb[32];
        int sl = 0;
        if (mb >= 1024) sl = 6; else sl = 5;
        for (int p = sl; p < 11; p++) wc(' ');

        print_pad(info.sectors, 9);
        ws("  ");

        ws(info.model);
        wn();
    }

    if (!found)
        ws("  (no disks detected)\n");
}