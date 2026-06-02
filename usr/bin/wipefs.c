#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/cervus.h>
#include <cervus_util.h>

#define WIPE_CHUNK_SECTORS 128
#define WIPE_HEAD_SECTORS  2048
#define WIPE_TAIL_SECTORS  64

static const char USAGE[] =
    "Usage: wipefs [-f] <device>\n"
    "Erase filesystem and partition-table signatures on a block device.\n"
    "Zeroes the first 1 MiB (MBR + protective MBR + GPT primary) and the\n"
    "last 32 KiB (GPT backup). The device's content beyond those ranges\n"
    "is left intact.\n";

static int find_disk(const char *name, cervus_disk_info_t *out)
{
    for (int i = 0; i < 64; i++) {
        cervus_disk_info_t info;
        memset(&info, 0, sizeof(info));
        if (cervus_disk_info(i, &info) < 0) break;
        if (!info.present) continue;
        if (strcmp(info.name, name) == 0) { *out = info; return 0; }
    }
    return -1;
}

int main(int argc, char **argv)
{
    if (cervus_check_help_version(argc, argv, USAGE, "wipefs")) return 0;

    int force = 0;
    const char *devname = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-f") == 0) { force = 1; continue; }
        if (is_shell_flag(argv[i])) continue;
        if (!devname) devname = argv[i];
    }
    if (!devname) { fputs(USAGE, stdout); return 1; }

    const char *short_name = devname;
    if (strncmp(short_name, "/dev/", 5) == 0) short_name += 5;

    cervus_disk_info_t info;
    if (find_disk(short_name, &info) < 0) {
        fprintf(stderr, "wipefs: device '%s' not found\n", short_name);
        return 1;
    }

    if (!force) {
        char target[128];
        snprintf(target, sizeof(target), "wipe signatures on /dev/%s", short_name);
        if (!cervus_confirm(target, NULL,
                "filesystem and partition table will be unrecoverable")) {
            fputs("wipefs: aborted\n", stderr);
            return 1;
        }
    }

    uint64_t total_sectors = info.sectors;
    if (total_sectors == 0) {
        fprintf(stderr, "wipefs: device reports 0 sectors\n");
        return 1;
    }

    size_t buf_bytes = WIPE_CHUNK_SECTORS * 512;
    uint8_t *zero = calloc(1, buf_bytes);
    if (!zero) { fputs("wipefs: out of memory\n", stderr); return 1; }

    uint64_t head_left = total_sectors < WIPE_HEAD_SECTORS ? total_sectors : WIPE_HEAD_SECTORS;
    uint64_t lba = 0;
    while (head_left > 0) {
        uint64_t n = head_left < WIPE_CHUNK_SECTORS ? head_left : WIPE_CHUNK_SECTORS;
        if (cervus_disk_write_raw(short_name, lba, n, zero) < 0) {
            fprintf(stderr, "wipefs: write_raw failed at LBA %lu\n", (unsigned long)lba);
            free(zero);
            return 1;
        }
        lba += n;
        head_left -= n;
    }
    printf("  head: zeroed first %lu sectors\n", (unsigned long)(lba));

    if (total_sectors > WIPE_TAIL_SECTORS + WIPE_HEAD_SECTORS) {
        uint64_t tail_start = total_sectors - WIPE_TAIL_SECTORS;
        uint64_t left = WIPE_TAIL_SECTORS;
        lba = tail_start;
        while (left > 0) {
            uint64_t n = left < WIPE_CHUNK_SECTORS ? left : WIPE_CHUNK_SECTORS;
            if (cervus_disk_write_raw(short_name, lba, n, zero) < 0) {
                fprintf(stderr, "wipefs: write_raw failed at LBA %lu\n", (unsigned long)lba);
                free(zero);
                return 1;
            }
            lba += n;
            left -= n;
        }
        printf("  tail: zeroed last %u sectors (GPT backup area)\n", WIPE_TAIL_SECTORS);
    }

    free(zero);
    printf("Done. /dev/%s wiped.\n", short_name);
    return 0;
}
