#include <stdio.h>
#include <string.h>
#include <stdint.h>
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

static const cervus_mount_info_t *find_mount_for_device(const cervus_mount_info_t *m, int n, const char *dev)
{
    for (int i = 0; i < n; i++) if (strcmp(m[i].device, dev) == 0) return &m[i];
    return NULL;
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
        case 0x82: return "Linux swap";
        case 0x83: return "Linux";
        case 0xA5: return "FreeBSD";
        case 0xEE: return "GPT protective";
        case 0xEF: return "EFI System";
        default:   return "Unknown";
    }
}

static int read_mbr_types(const char *disk, uint8_t ty[4], uint32_t st[4], uint32_t ct[4], uint8_t bt[4])
{
    uint8_t sec[512];
    if (cervus_disk_read_raw(disk, 0, 1, sec) < 0) return -1;
    for (int i = 0; i < 4; i++) {
        uint8_t *e = sec + 0x1BE + i * 16;
        bt[i] = e[0];
        ty[i] = e[4];
        st[i] = e[8]  | (e[9]  << 8) | (e[10] << 16) | (e[11] << 24);
        ct[i] = e[12] | (e[13] << 8) | (e[14] << 16) | (e[15] << 24);
    }
    return 0;
}

static void print_disk(const cervus_disk_info_t *d,
                       const cervus_part_info_t *parts, int nparts,
                       const cervus_mount_info_t *mounts, int nmounts)
{
    fputs(C_BOLD C_CYAN "Device" C_RESET "\n", stdout);
    printf("  Name       : %s\n", d->name);
    printf("  Model      : %s\n", d->model[0] ? d->model : "(unknown)");
    fputs( "  Transport  : ATA/IDE (PIO)\n", stdout);
    fputs( "  Size       : ", stdout); print_size(d->size_bytes);
    printf(" (%lu sectors, 512 B each)\n\n", (unsigned long)d->sectors);

    fputs(C_BOLD C_CYAN "Partitions" C_RESET "\n", stdout);

    uint8_t  ty[4] = {0}; uint32_t st[4] = {0}; uint32_t ct[4] = {0}; uint8_t bt[4] = {0};
    int got_mbr = (read_mbr_types(d->name, ty, st, ct, bt) == 0);

    fputs("  " C_BOLD "NAME    TYPE             LBA START   SECTORS    SIZE      BOOT" C_RESET "\n", stdout);
    fputs("  -------------------------------------------------------------------\n", stdout);

    int any_part = 0;
    for (int i = 0; i < nparts; i++) {
        const cervus_part_info_t *p = &parts[i];
        if (strcmp(p->disk_name, d->name) != 0) continue;
        if (strcmp(p->part_name, d->name) == 0) continue;
        any_part = 1;

        uint8_t  t = 0; uint32_t lba = 0, sectors = 0; int boot = 0;
        if (got_mbr && p->part_num >= 1 && p->part_num <= 4) {
            t       = ty[p->part_num - 1];
            lba     = st[p->part_num - 1];
            sectors = ct[p->part_num - 1];
            boot    = (bt[p->part_num - 1] & 0x80) ? 1 : 0;
        }
        uint64_t sz = (uint64_t)sectors * 512ULL;
        printf("  %-6s  %02x %-14s  %10u  %9u  %6lu M  %s\n",
               p->part_name, t, type_to_name(t),
               lba, sectors, (unsigned long)(sz / (1024 * 1024)),
               boot ? "*" : "-");
    }
    if (!any_part) fputs("  (no partitions - disk not partitioned)\n", stdout);
    putchar('\n');

    fputs(C_BOLD C_CYAN "Mount points" C_RESET "\n", stdout);
    fputs("  " C_BOLD "DEVICE    FSTYPE    MOUNTPOINT" C_RESET "\n", stdout);
    fputs("  -----------------------------------------------\n", stdout);
    int any_mount = 0;
    for (int i = 0; i < nparts; i++) {
        const cervus_part_info_t *p = &parts[i];
        if (strcmp(p->disk_name, d->name) != 0) continue;
        const cervus_mount_info_t *m = find_mount_for_device(mounts, nmounts, p->part_name);
        if (!m) continue;
        any_mount = 1;
        printf("  %-8s  %-8s  %s\n", m->device, m->fstype, m->path);
    }
    if (!any_mount) fputs("  (no partitions of this disk are mounted)\n", stdout);
    putchar('\n');

    fputs(C_BOLD C_CYAN "Filesystem usage" C_RESET "\n", stdout);
    int any_fs = 0;
    for (int i = 0; i < nparts; i++) {
        const cervus_part_info_t *p = &parts[i];
        if (strcmp(p->disk_name, d->name) != 0) continue;
        const cervus_mount_info_t *m = find_mount_for_device(mounts, nmounts, p->part_name);
        if (!m) continue;
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

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    cervus_disk_info_t disks[8];
    int ndisks = 0;
    for (int i = 0; i < 8; i++) {
        memset(&disks[ndisks], 0, sizeof(disks[0]));
        int r = cervus_disk_info(i, &disks[ndisks]);
        if (r < 0) break;
        if (!disks[ndisks].present) continue;

        int is_part = 0;
        for (size_t k = 0; disks[ndisks].name[k]; k++)
            if (disks[ndisks].name[k] >= '0' && disks[ndisks].name[k] <= '9') { is_part = 1; break; }
        if (is_part) continue;
        ndisks++;
        if (ndisks >= 8) break;
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
