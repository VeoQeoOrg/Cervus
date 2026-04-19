#include "cervus_user.h"

#define SYS_DISK_INFO       533
#define SYS_DISK_LIST_PARTS 544
#define SYS_LIST_MOUNTS     546
#define SYS_STATVFS         547

typedef struct {
	char     name[32];
	uint64_t sectors;
	uint64_t size_bytes;
	char     model[41];
	uint8_t  present;
	uint8_t  _pad[6];
} disk_info_t;

typedef struct {
	char     path[512];
	char     device[32];
	char     fstype[16];
	uint32_t flags;
} mount_info_t;

static inline int disk_info_sc(int i, disk_info_t *info)
{
	return (int)syscall2(SYS_DISK_INFO, i, info);
}

static inline long list_mounts_sc(mount_info_t *out, int m)
{
	return syscall2(SYS_LIST_MOUNTS, (long)out, (long)m);
}

static inline long list_parts_sc(cervus_part_info_t *out, int m)
{
	return syscall2(SYS_DISK_LIST_PARTS, (long)out, (long)m);
}

static inline long statvfs_sc(const char *path, cervus_statvfs_t *s)
{
	return syscall2(SYS_STATVFS, (long)path, (long)s);
}

static void print_size(uint64_t bytes)
{
	uint64_t mb = bytes / (1024 * 1024);
	if (mb >= 1024) {
		uint64_t gb10 = (bytes * 10) / (1024ULL * 1024ULL * 1024ULL);
		print_u64(gb10 / 10);
		wc('.');
		print_u64(gb10 % 10);
		ws(" GiB");
	} else if (mb > 0) {
		print_u64(mb);
		ws(" MiB");
	} else {
		print_u64(bytes / 1024);
		ws(" KiB");
	}
}

static void print_percent_bar(uint64_t used, uint64_t total, int width)
{
	if (total == 0) {
		for (int i = 0; i < width; i++) wc('-');
		return;
	}
	uint64_t filled = (used * (uint64_t)width) / total;
	if (filled > (uint64_t)width) filled = width;
	wc('[');
	for (uint64_t i = 0; i < filled; i++) wc('#');
	for (uint64_t i = filled; i < (uint64_t)width; i++) wc('.');
	wc(']');
}

static void print_percent(uint64_t used, uint64_t total)
{
	if (total == 0) { ws("  -"); return; }
	uint64_t p10 = (used * 1000ULL) / total;
	uint64_t pct = p10 / 10;
	uint64_t frac = p10 % 10;
	if (pct < 10)  wc(' ');
	if (pct < 100) wc(' ');
	print_u64(pct);
	wc('.');
	print_u64(frac);
	wc('%');
}

static const mount_info_t *find_mount_for_device(const mount_info_t *mounts, int n, const char *devname)
{
	for (int i = 0; i < n; i++) {
		if (strcmp(mounts[i].device, devname) == 0) return &mounts[i];
	}
	return NULL;
}

static void pad_to_col(int current, int target)
{
	for (int i = current; i < target; i++) wc(' ');
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

static int read_mbr_types(const char *disk_name,
                          uint8_t out_types[4], uint32_t out_starts[4],
                          uint32_t out_counts[4], uint8_t out_boot[4])
{
	uint8_t sec[512];
	int r = cervus_disk_read_raw(disk_name, 0, 1, sec);
	if (r < 0) return -1;
	for (int i = 0; i < 4; i++) {
		uint8_t *e = sec + 0x1BE + i * 16;
		out_boot[i]   = e[0];
		out_types[i]  = e[4];
		out_starts[i] = e[8]  | (e[9]  << 8) | (e[10] << 16) | (e[11] << 24);
		out_counts[i] = e[12] | (e[13] << 8) | (e[14] << 16) | (e[15] << 24);
	}
	return 0;
}

static void print_disk(const disk_info_t *d,
                       const cervus_part_info_t *parts, int nparts,
                       const mount_info_t *mounts, int nmounts)
{
	ws(C_BOLD C_CYAN "Device" C_RESET "\n");
	ws("  Name       : "); ws(d->name); wn();
	ws("  Model      : "); ws(d->model[0] ? d->model : "(unknown)"); wn();
	ws("  Transport  : ATA/IDE (PIO)\n");
	ws("  Size       : "); print_size(d->size_bytes);
	ws("  ("); print_u64(d->sectors); ws(" sectors, 512 B each)\n");
	wn();

	ws(C_BOLD C_CYAN "Partitions" C_RESET "\n");

	uint8_t  mbr_types[4]  = {0};
	uint32_t mbr_starts[4] = {0};
	uint32_t mbr_counts[4] = {0};
	uint8_t  mbr_boot[4]   = {0};
	int got_mbr = (read_mbr_types(d->name, mbr_types, mbr_starts, mbr_counts, mbr_boot) == 0);

	ws("  " C_BOLD "NAME    TYPE             LBA START   SECTORS    SIZE      BOOT" C_RESET "\n");
	ws("  -------------------------------------------------------------------\n");

	int any_part = 0;
	for (int i = 0; i < nparts; i++) {
		const cervus_part_info_t *p = &parts[i];
		if (strcmp(p->disk_name, d->name) != 0) continue;
		if (strcmp(p->part_name, d->name) == 0) continue;
		any_part = 1;

		uint8_t  t        = 0;
		uint32_t lba      = 0;
		uint32_t sectors  = 0;
		int      bootable = 0;
		if (got_mbr && p->part_num >= 1 && p->part_num <= 4) {
			t        = mbr_types[p->part_num - 1];
			lba      = mbr_starts[p->part_num - 1];
			sectors  = mbr_counts[p->part_num - 1];
			bootable = (mbr_boot[p->part_num - 1] & 0x80) ? 1 : 0;
		}

		ws("  ");
		ws(p->part_name);
		pad_to_col((int)strlen(p->part_name), 8);

		print_hex_pad0(t, 2);
		wc(' ');
		const char *tname = type_to_name(t);
		ws(tname);
		int tl = 3 + (int)strlen(tname);
		for (int k = tl; k < 17; k++) wc(' ');

		print_pad(lba, 10);     ws("  ");
		print_pad(sectors, 9);  ws("  ");

		uint64_t sz = (uint64_t)sectors * 512ULL;
		int mb = (int)(sz / (1024 * 1024));
		print_pad((uint64_t)mb, 6); ws(" M  ");

		ws(bootable ? "*" : "-");
		wn();
	}
	if (!any_part) ws("  (no partitions — disk not partitioned)\n");
	wn();

	ws(C_BOLD C_CYAN "Mount points" C_RESET "\n");
	ws("  " C_BOLD "DEVICE    FSTYPE    MOUNTPOINT" C_RESET "\n");
	ws("  -----------------------------------------------\n");

	int any_mount = 0;
	for (int i = 0; i < nparts; i++) {
		const cervus_part_info_t *p = &parts[i];
		if (strcmp(p->disk_name, d->name) != 0) continue;
		const mount_info_t *m = find_mount_for_device(mounts, nmounts, p->part_name);
		if (!m) continue;
		any_mount = 1;

		ws("  ");
		ws(m->device);
		pad_to_col((int)strlen(m->device), 10);
		ws(m->fstype);
		pad_to_col((int)strlen(m->fstype), 10);
		ws(m->path);
		wn();
	}
	if (!any_mount) ws("  (no partitions of this disk are mounted)\n");
	wn();

	ws(C_BOLD C_CYAN "Filesystem usage" C_RESET "\n");
	int any_fs = 0;
	for (int i = 0; i < nparts; i++) {
		const cervus_part_info_t *p = &parts[i];
		if (strcmp(p->disk_name, d->name) != 0) continue;
		const mount_info_t *m = find_mount_for_device(mounts, nmounts, p->part_name);
		if (!m) continue;

		cervus_statvfs_t s;
		if (statvfs_sc(m->path, &s) < 0) continue;
		any_fs = 1;

		uint64_t total = s.f_blocks * s.f_bsize;
		uint64_t free  = s.f_bfree  * s.f_bsize;
		uint64_t used  = (s.f_blocks >= s.f_bfree)
		                  ? (s.f_blocks - s.f_bfree) * s.f_bsize
		                  : 0;

		ws("  ");
		ws(C_BOLD);
		ws(m->path);
		ws(C_RESET " (");
		ws(m->fstype);
		ws(")\n");

		ws("    Block size : "); print_u64(s.f_bsize); ws(" B\n");
		ws("    Total      : "); print_size(total); wn();
		ws("    Used       : "); print_size(used);
		ws("   "); print_percent(used, total); wn();
		ws("    Free       : "); print_size(free); wn();

		if (s.f_files > 0) {
			ws("    Inodes     : ");
			print_u64(s.f_files - s.f_ffree);
			ws(" / ");
			print_u64(s.f_files);
			ws(" used (");
			print_u64(s.f_ffree);
			ws(" free)\n");
		}

		ws("    Usage      : ");
		print_percent_bar(used, total, 30);
		wc(' ');
		print_percent(used, total);
		wn();
		wn();
	}
	if (!any_fs) {
		ws("  (no mounted filesystems on this disk)\n");
		wn();
	}
}

CERVUS_MAIN(main)
{
	(void)argc;
	(void)argv;

	disk_info_t disks[8];
	int ndisks = 0;
	for (int i = 0; i < 8; i++) {
		memset(&disks[ndisks], 0, sizeof(disks[0]));
		int r = disk_info_sc(i, &disks[ndisks]);
		if (r < 0) break;
		if (!disks[ndisks].present) continue;

		int is_part = 0;
		for (size_t k = 0; disks[ndisks].name[k]; k++) {
			if (disks[ndisks].name[k] >= '0' && disks[ndisks].name[k] <= '9') {
				is_part = 1;
				break;
			}
		}
		if (is_part) continue;
		ndisks++;
		if (ndisks >= 8) break;
	}

	if (ndisks == 0) {
		ws(C_RED "  No disks detected.\n" C_RESET);
		exit(1);
	}

	cervus_part_info_t parts[16];
	long nparts = list_parts_sc(parts, 16);
	if (nparts < 0) nparts = 0;

	mount_info_t mounts[16];
	long nmounts = list_mounts_sc(mounts, 16);
	if (nmounts < 0) nmounts = 0;

	for (int i = 0; i < ndisks; i++) {
		if (i > 0) ws(C_GRAY "======================================================" C_RESET "\n\n");
		print_disk(&disks[i], parts, (int)nparts, mounts, (int)nmounts);
	}
}