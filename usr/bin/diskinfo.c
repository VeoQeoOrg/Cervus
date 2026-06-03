#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <sys/cervus.h>
#include <cervus_util.h>

static void print_size(uint64_t bytes)
{
    uint64_t mb = bytes / (1024 * 1024);
    if (mb >= 1024) {
        uint64_t gb10 = (bytes * 10) / (1024ULL * 1024 * 1024);
        printf("%lu.%lu GiB", (unsigned long)(gb10 / 10), (unsigned long)(gb10 % 10));
    } else if (mb > 0) {
        printf("%lu MiB", (unsigned long)mb);
    } else {
        printf("%lu KiB", (unsigned long)(bytes / 1024));
    }
}

static void print_percent_bar(uint64_t used, uint64_t total, int width)
{
    if (total == 0) { for (int i = 0; i < width; i++) putchar('-'); return; }
    uint64_t filled = (used * (uint64_t)width) / total;
    if (filled > (uint64_t)width) filled = width;
    putchar('[');
    for (uint64_t i = 0; i < filled; i++) putchar('#');
    for (uint64_t i = filled; i < (uint64_t)width; i++) putchar('.');
    putchar(']');
}

static void print_percent(uint64_t used, uint64_t total)
{
    if (total == 0) { fputs("  -", stdout); return; }
    uint64_t p10 = (used * 1000ULL) / total;
    printf("%3lu.%lu%%", (unsigned long)(p10 / 10), (unsigned long)(p10 % 10));
}

static const char *type_to_name(uint8_t t)
{
    switch (t) {
        case 0x00: return "empty";
        case 0x01: return "FAT12";
        case 0x04: return "FAT16 (<32M)";
        case 0x05: return "Extended";
        case 0x06: return "FAT16";
        case 0x07: return "NTFS/exFAT";
        case 0x0B: return "FAT32";
        case 0x0C: return "FAT32 (LBA)";
        case 0x0E: return "FAT16 (LBA)";
        case 0x82: return "Linux-compatible swap";
        case 0x83: return "Linux-compatible";
        case 0xA5: return "FreeBSD";
        case 0xEE: return "GPT protective";
        case 0xEF: return "EFI System";
        default:   return "Unknown";
    }
}

static int read_mbr_types(const char *disk, uint32_t sec_size,
                          uint8_t ty[4], uint32_t st[4], uint32_t ct[4], uint8_t bt[4])
{
    if (sec_size == 0) sec_size = 512;
    if (sec_size > 4096) return -1;
    uint8_t sec[4096];
    if (cervus_disk_read_raw(disk, 0, 1, sec) < 0) return -1;
    if (sec[510] != 0x55 || sec[511] != 0xAA) return -1;
    for (int i = 0; i < 4; i++) {
        uint8_t *e = sec + 0x1BE + i * 16;
        bt[i] = e[0];
        ty[i] = e[4];
        st[i] = e[8]  | (e[9]  << 8) | (e[10] << 16) | (e[11] << 24);
        ct[i] = e[12] | (e[13] << 8) | (e[14] << 16) | (e[15] << 24);
    }
    return 0;
}

static const char *transport_for(const char *name)
{
    if (!name || !name[0]) return "Unknown";
    if (name[0] == 's' && name[1] == 'r') return "AHCI ATAPI (CD/DVD)";
    if (name[0] == 's' && name[1] == 'd') return "AHCI/SATA (DMA)";
    if (name[0] == 'h' && name[1] == 'd') return "ATA/IDE (PIO)";
    if (name[0] == 'n' && name[1] == 'v' && name[2] == 'm' && name[3] == 'e')
        return "NVMe";
    if (name[0] == 'u' && name[1] == 's' && name[2] == 'b')
        return "USB Mass Storage (BBB)";
    if (name[0] == 'u' && name[1] == 'h' && name[2] == 'd')
        return "USB Mass Storage (BBB)";
    return "Unknown";
}

static void print_disk(const cervus_disk_info_t *d,
                       const cervus_part_info_t *parts, int nparts,
                       const cervus_mount_info_t *mounts, int nmounts)
{
    uint32_t sec_size = d->sector_size ? d->sector_size : 512;

    fputs(C_BOLD C_CYAN "Device" C_RESET "\n", stdout);
    printf("  Name       : %s\n", d->name);
    printf("  Model      : %s\n", d->model[0] ? d->model : "(unknown)");
    printf("  Transport  : %s\n", transport_for(d->name));
    fputs( "  Size       : ", stdout); print_size(d->size_bytes);
    printf(" (%lu sectors, %u B each)\n\n", (unsigned long)d->sectors, sec_size);

    fputs(C_BOLD C_CYAN "Partitions" C_RESET "\n", stdout);

    uint8_t  ty[4] = {0}; uint32_t st[4] = {0}; uint32_t ct[4] = {0}; uint8_t bt[4] = {0};
    int got_mbr = (sec_size == 512)
                ? (read_mbr_types(d->name, sec_size, ty, st, ct, bt) == 0)
                : 0;

    fputs("  " C_BOLD "NAME    TYPE                     LBA START   SECTORS    SIZE      BOOT" C_RESET "\n", stdout);
    fputs("  ---------------------------------------------------------------------------\n", stdout);

    int any_part = 0;
    for (int i = 0; i < nparts; i++) {
        const cervus_part_info_t *p = &parts[i];
        if (strcmp(p->disk_name, d->name) != 0) continue;
        if (strcmp(p->part_name, d->name) == 0) continue;
        any_part = 1;

        uint8_t  t       = p->type;
        uint64_t lba     = p->lba_start;
        uint64_t sectors = p->sector_count;
        int      boot    = p->bootable ? 1 : 0;

        if (t == 0 && got_mbr && p->part_num >= 1 && p->part_num <= 4) {
            uint8_t mt = ty[p->part_num - 1];
            if (mt != 0) {
                t = mt;
                boot = (bt[p->part_num - 1] & 0x80) ? 1 : 0;
            }
        }
        if (lba == 0 && got_mbr && p->part_num >= 1 && p->part_num <= 4 &&
            st[p->part_num - 1] != 0)
            lba = st[p->part_num - 1];
        if (sectors == 0 && got_mbr && p->part_num >= 1 && p->part_num <= 4 &&
            ct[p->part_num - 1] != 0)
            sectors = ct[p->part_num - 1];

        uint64_t sz = sectors * (uint64_t)sec_size;
        printf("  %-6s  %02x %-21s  %10lu  %9lu  %6lu M  %s\n",
               p->part_name, t, type_to_name(t),
               (unsigned long)lba, (unsigned long)sectors,
               (unsigned long)(sz / (1024 * 1024)),
               boot ? "*" : "-");
    }
    if (!any_part) fputs("  (no partitions - disk not partitioned)\n", stdout);
    putchar('\n');

    fputs(C_BOLD C_CYAN "Mount points" C_RESET "\n", stdout);
    fputs("  " C_BOLD "DEVICE    FSTYPE    MOUNTPOINT" C_RESET "\n", stdout);
    fputs("  -----------------------------------------------\n", stdout);
    int any_mount = 0;
    for (int i = 0; i < nmounts; i++) {
        const cervus_mount_info_t *m = &mounts[i];
        int belongs = 0;
        if (strcmp(m->device, d->name) == 0) belongs = 1;
        for (int j = 0; !belongs && j < nparts; j++) {
            if (strcmp(parts[j].disk_name, d->name) != 0) continue;
            if (strcmp(parts[j].part_name, m->device) == 0) belongs = 1;
        }
        if (!belongs) continue;
        any_mount = 1;
        printf("  " C_GREEN "%-8s" C_RESET "  %-8s  " C_BOLD "%s" C_RESET "\n",
               m->device, m->fstype, m->path);
    }
    if (!any_mount) fputs("  " C_GRAY "(no partitions of this disk are mounted)" C_RESET "\n", stdout);
    putchar('\n');

    fputs(C_BOLD C_CYAN "Filesystem usage" C_RESET "\n", stdout);
    int any_fs = 0;
    for (int i = 0; i < nmounts; i++) {
        const cervus_mount_info_t *m = &mounts[i];
        int belongs = 0;
        if (strcmp(m->device, d->name) == 0) belongs = 1;
        for (int j = 0; !belongs && j < nparts; j++) {
            if (strcmp(parts[j].disk_name, d->name) != 0) continue;
            if (strcmp(parts[j].part_name, m->device) == 0) belongs = 1;
        }
        if (!belongs) continue;
        cervus_statvfs_t s;
        if (cervus_statvfs(m->path, &s) < 0) continue;
        any_fs = 1;

        uint64_t total = s.f_blocks * s.f_bsize;
        uint64_t free  = s.f_bfree  * s.f_bsize;
        uint64_t used  = (s.f_blocks >= s.f_bfree)
                       ? (s.f_blocks - s.f_bfree) * s.f_bsize : 0;

        printf("  " C_BOLD "%s" C_RESET " (%s)\n", m->path, m->fstype);
        printf("    Block size : %lu B\n",        (unsigned long)s.f_bsize);
        fputs( "    Total      : ", stdout); print_size(total); putchar('\n');
        fputs( "    Used       : ", stdout); print_size(used);
        fputs( "   ", stdout); print_percent(used, total); putchar('\n');
        fputs( "    Free       : ", stdout); print_size(free); putchar('\n');
        if (s.f_files > 0) {
            printf("    Inodes     : %lu / %lu used (%lu free)\n",
                   (unsigned long)(s.f_files - s.f_ffree),
                   (unsigned long)s.f_files,
                   (unsigned long)s.f_ffree);
        }
        fputs("    Usage      : ", stdout);
        print_percent_bar(used, total, 30);
        putchar(' ');
        print_percent(used, total);
        fputs("\n\n", stdout);
    }
    if (!any_fs) fputs("  (no mounted filesystems on this disk)\n\n", stdout);
}


static const char USAGE[] =
    "Usage: diskinfo\nShow available disk devices.\n";
int main(int argc, char **argv)
{
    if (cervus_check_help_version(argc, argv, USAGE, "diskinfo")) return 0;
    (void)argc; (void)argv;

    cervus_disk_info_t disks[16];
    int ndisks = 0;
    for (int i = 0; i < 64; i++) {
        cervus_disk_info_t info;
        memset(&info, 0, sizeof(info));
        int r = cervus_disk_info(i, &info);
        if (r == -ERANGE) break;
        if (r < 0) continue;
        if (!info.present) continue;
        if (info.is_partition) continue;
        disks[ndisks++] = info;
        if (ndisks >= (int)(sizeof(disks) / sizeof(disks[0]))) break;
    }

    if (ndisks == 0) {
        fputs(C_RED "  No disks detected.\n" C_RESET, stdout);
        return 1;
    }

    cervus_part_info_t parts[16];
    long nparts = cervus_disk_list_parts(parts, 16);
    if (nparts < 0) nparts = 0;

    cervus_mount_info_t mounts[16];
    long nmounts = cervus_list_mounts(mounts, 16);
    if (nmounts < 0) nmounts = 0;

    for (int i = 0; i < ndisks; i++) {
        if (i > 0) fputs(C_GRAY "======================================================" C_RESET "\n\n", stdout);
        print_disk(&disks[i], parts, (int)nparts, mounts, (int)nmounts);
    }
    return 0;
}
