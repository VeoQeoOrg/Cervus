#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/cervus.h>
#include <cervus_util.h>

#define MAX_PARTS 4
#define ALIGN_SECTORS 2048

static const char USAGE[] =
    "Usage: mkpart [-f] <device> <type>:<size> [<type>:<size> ...]\n"
    "\n"
    "Write an MBR with up to 4 partitions. Sizes can be:\n"
    "  <N>MiB | <N>GiB | <N>%   percentage of remaining space\n"
    "  rest                     all remaining sectors\n"
    "\n"
    "Type is an MBR partition type byte in hex:\n"
    "  0C  FAT32 (LBA)              82  Linux swap\n"
    "  83  Linux native             EF  EFI System (ESP)\n"
    "  EE  Protective MBR (GPT)     07  NTFS/exFAT\n"
    "\n"
    "First partition is marked bootable. Partitions are aligned to 1 MiB.\n"
    "\n"
    "Example:\n"
    "  mkpart sda 0C:64MiB 83:rest\n";

static int parse_size(const char *s, uint64_t total_left, uint64_t *out_sectors)
{
    char *end = NULL;
    if (strcmp(s, "rest") == 0 || strcmp(s, "*") == 0) {
        *out_sectors = total_left;
        return 0;
    }
    unsigned long long v = strtoull(s, &end, 10);
    if (end == s || v == 0) return -1;

    if (*end == '%') {
        if (v > 100) return -1;
        *out_sectors = (total_left * v) / 100;
    } else if (strcmp(end, "MiB") == 0 || strcmp(end, "M") == 0 || strcmp(end, "m") == 0) {
        *out_sectors = (v * 1024ULL * 1024ULL) / 512ULL;
    } else if (strcmp(end, "GiB") == 0 || strcmp(end, "G") == 0 || strcmp(end, "g") == 0) {
        *out_sectors = (v * 1024ULL * 1024ULL * 1024ULL) / 512ULL;
    } else if (strcmp(end, "KiB") == 0 || strcmp(end, "K") == 0 || strcmp(end, "k") == 0) {
        *out_sectors = (v * 1024ULL) / 512ULL;
    } else if (*end == '\0') {
        *out_sectors = v;
    } else {
        return -1;
    }
    if (*out_sectors == 0) return -1;
    if (*out_sectors > total_left) *out_sectors = total_left;
    return 0;
}

static int parse_type(const char *s, uint8_t *out)
{
    char *end = NULL;
    unsigned long v = strtoul(s, &end, 16);
    if (end == s || *end != '\0' || v > 0xFF) return -1;
    *out = (uint8_t)v;
    return 0;
}

static int find_disk(const char *name, cervus_disk_info_t *out)
{
    for (int i = 0; i < 64; i++) {
        cervus_disk_info_t info;
        memset(&info, 0, sizeof(info));
        if (cervus_disk_info(i, &info) < 0) break;
        if (!info.present || info.is_partition) continue;
        if (strcmp(info.name, name) == 0) { *out = info; return 0; }
    }
    return -1;
}

int main(int argc, char **argv)
{
    if (cervus_check_help_version(argc, argv, USAGE, "mkpart")) return 0;

    int force = 0;
    const char *devname = NULL;
    const char *specs[MAX_PARTS] = {0};
    int n_specs = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-f") == 0) { force = 1; continue; }
        if (!devname) { devname = argv[i]; continue; }
        if (n_specs < MAX_PARTS) specs[n_specs++] = argv[i];
        else { fputs("mkpart: too many partitions (max 4 for MBR)\n", stderr); return 1; }
    }

    if (!devname || n_specs == 0) { fputs(USAGE, stdout); return 1; }

    const char *short_name = devname;
    if (strncmp(short_name, "/dev/", 5) == 0) short_name += 5;

    cervus_disk_info_t info;
    if (find_disk(short_name, &info) < 0) {
        fprintf(stderr, "mkpart: device '%s' not found (whole disks only)\n", short_name);
        return 1;
    }

    cervus_mbr_part_t parts[MAX_PARTS];
    memset(parts, 0, sizeof(parts));

    uint64_t cur_lba = ALIGN_SECTORS;
    uint64_t end_lba = info.sectors;
    if (cur_lba >= end_lba) {
        fprintf(stderr, "mkpart: device too small\n");
        return 1;
    }

    for (int i = 0; i < n_specs; i++) {
        char tmp[64];
        snprintf(tmp, sizeof(tmp), "%s", specs[i]);
        char *colon = strchr(tmp, ':');
        if (!colon) {
            fprintf(stderr, "mkpart: spec '%s' has no <type>:<size>\n", specs[i]);
            return 1;
        }
        *colon = '\0';
        const char *type_s = tmp;
        const char *size_s = colon + 1;

        uint8_t type = 0;
        if (parse_type(type_s, &type) < 0) {
            fprintf(stderr, "mkpart: bad partition type '%s' (use 2-digit hex)\n", type_s);
            return 1;
        }

        uint64_t avail = end_lba - cur_lba;
        uint64_t count = 0;
        if (parse_size(size_s, avail, &count) < 0) {
            fprintf(stderr, "mkpart: bad size '%s'\n", size_s);
            return 1;
        }
        if (count > avail) count = avail;

        parts[i].boot_flag    = (i == 0) ? 1 : 0;
        parts[i].type         = type;
        parts[i].lba_start    = (uint32_t)cur_lba;
        parts[i].sector_count = (uint32_t)count;

        printf("  [%d] type=0x%02X  lba=%lu..%lu  (%lu sectors, %lu MiB)\n",
               i + 1, type,
               (unsigned long)cur_lba,
               (unsigned long)(cur_lba + count - 1),
               (unsigned long)count,
               (unsigned long)((count * 512ULL) / (1024ULL * 1024ULL)));

        cur_lba += count;
    }

    if (!force) {
        char target[128];
        snprintf(target, sizeof(target), "write new MBR to /dev/%s", short_name);
        if (!cervus_confirm(target, NULL,
                "the existing partition table will be overwritten")) {
            fputs("mkpart: aborted\n", stderr);
            return 1;
        }
    }

    int r = cervus_disk_partition(short_name, parts, (uint64_t)n_specs);
    if (r < 0) {
        fprintf(stderr, "mkpart: sys_disk_partition failed (%d)\n", r);
        return 1;
    }
    printf("MBR written. %d partition(s) created on /dev/%s.\n", n_specs, short_name);
    return 0;
}
