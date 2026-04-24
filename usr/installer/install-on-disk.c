#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/cervus.h>
#include <sys/syscall.h>
#include <cervus_util.h>

#define MAX_ENTRIES 128

#define MBR_TYPE_FAT32_LBA  0x0C
#define MBR_TYPE_LINUX      0x83
#define MBR_TYPE_LINUX_SWAP 0x82

typedef struct {
	char     name[32];
	char     model[41];
	uint64_t sectors;
	uint64_t size_bytes;
} disk_summary_t;

typedef struct {
	char    name[64];
	uint8_t type;
} dir_entry_t;

static void safe_strcpy(char *dst, size_t dsz, const char *src)
{
	if (!dst || dsz == 0) return;
	if (!src) { dst[0] = '\0'; return; }
	size_t i = 0;
	while (i + 1 < dsz && src[i]) { dst[i] = src[i]; i++; }
	dst[i] = '\0';
}

static void clear_screen(void)
{
	fputs("\x1b[2J\x1b[H", stdout);
}

static int ensure_dir(const char *path)
{
	struct stat st;
	if (stat(path, &st) == 0) return 0;
	return mkdir(path, 0755);
}

static int ensure_parent_dir(const char *path)
{
	char tmp[512];
	size_t len = strlen(path);
	if (len >= sizeof(tmp)) return -1;
	int last_slash = -1;
	for (int i = (int)len - 1; i >= 0; i--) {
		if (path[i] == '/') { last_slash = i; break; }
	}
	if (last_slash <= 0) return 0;
	for (int i = 0; i < last_slash; i++) tmp[i] = path[i];
	tmp[last_slash] = '\0';
	int depth = 0;
	int starts[32];
	starts[depth++] = 0;
	for (int i = 1; i < last_slash && depth < 32; i++) {
		if (tmp[i] == '/') starts[depth++] = i;
	}
	for (int d = 0; d < depth; d++) {
		int end = (d + 1 < depth) ? starts[d + 1] : last_slash;
		char part[512];
		for (int i = 0; i < end; i++) part[i] = tmp[i];
		part[end] = '\0';
		if (part[0] == '\0') continue;
		struct stat st;
		if (stat(part, &st) != 0) syscall2(SYS_MKDIR, part, 0755);
	}
	return 0;
}

static int copy_one_file_progress(const char *src, const char *dst, const char *display_name)
{
	int sfd = open(src, O_RDONLY, 0);
	if (sfd < 0) return sfd;
	ensure_parent_dir(dst);
	int dfd = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0755);
	if (dfd < 0) { close(sfd); return dfd; }

	struct stat st;
	uint64_t total = 0;
	if (stat(src, &st) == 0) total = st.st_size;

	static char fbuf[4096];
	ssize_t n;
	int rc = 0;
	uint64_t written = 0;
	int last_pct = -1;
	int spinner = 0;

	while ((n = read(sfd, fbuf, sizeof(fbuf))) > 0) {
		ssize_t w = write(dfd, fbuf, (size_t)n);
		if (w < 0) { rc = (int)w; break; }
		written += (uint64_t)w;
		if (total > 0 && display_name) {
			int pct = (int)((written * 100) / total);
			if (pct != last_pct) {
				static const char glyphs[4] = { '|', '/', '-', '\\' };
				fputs("\r\033[K       ", stdout);
				putchar(glyphs[spinner & 3]);
				fputs(" ", stdout);
				fputs(display_name, stdout);
				fputs(" ", stdout);
				char pb[8];
				int bi = 0;
				if (pct >= 100) { pb[bi++]='1'; pb[bi++]='0'; pb[bi++]='0'; }
				else if (pct >= 10) { pb[bi++]=(char)('0'+pct/10); pb[bi++]=(char)('0'+pct%10); }
				else { pb[bi++]=(char)('0'+pct); }
				pb[bi++]='%';
				pb[bi]='\0';
				fputs(pb, stdout);
				spinner++;
				last_pct = pct;
			}
		}
	}
	close(sfd);
	close(dfd);

	if (display_name) {
		fputs("\r\033[K       ", stdout);
		fputs(display_name, stdout);
		fputs(rc < 0 ? " FAILED\n" : " done\n", stdout);
	}
	return rc;
}

static int copy_one_file(const char *src, const char *dst)
{
	return copy_one_file_progress(src, dst, NULL);
}

static int read_dir_entries(const char *path, dir_entry_t *out, int max)
{
	DIR *d = opendir(path);
	if (!d) return 0;
	int count = 0;
	struct dirent *de;
	while (count < max && (de = readdir(d)) != NULL) {
		if (de->d_name[0] == '.' && (de->d_name[1] == '\0' ||
		    (de->d_name[1] == '.' && de->d_name[2] == '\0'))) continue;
		safe_strcpy(out[count].name, sizeof(out[count].name), de->d_name);
		out[count].type = de->d_type;
		count++;
	}
	closedir(d);
	return count;
}

static int should_exclude_from_copy(const char *name)
{
	if (strcmp(name, "install-on-disk") == 0) return 1;
	return 0;
}

static void copy_tree(const char *src_dir, const char *dst_dir)
{
	dir_entry_t entries[MAX_ENTRIES];
	int n = read_dir_entries(src_dir, entries, MAX_ENTRIES);
	if (n == 0) return;
	ensure_dir(dst_dir);
	for (int i = 0; i < n; i++) {
		if (should_exclude_from_copy(entries[i].name)) {
			fputs(C_GRAY "       skip (installer): " C_RESET, stdout);
			fputs(entries[i].name, stdout);
			putchar(10);
			continue;
		}
		char sp[256], dp[256];
		path_join(src_dir, entries[i].name, sp, sizeof(sp));
		path_join(dst_dir, entries[i].name, dp, sizeof(dp));
		if (entries[i].type == 1) {
			ensure_dir(dp);
			copy_tree(sp, dp);
		} else {
			copy_one_file_progress(sp, dp, sp);
		}
	}
}

static int list_disks(disk_summary_t out[4])
{
	int found = 0;
	for (int i = 0; i < 4; i++) {
		struct {
			char     name[32];
			uint64_t sectors;
			uint64_t size_bytes;
			char     model[41];
			uint8_t  present;
			uint8_t  _pad[6];
		} info;
		memset(&info, 0, sizeof(info));
		int r = (int)syscall2(SYS_DISK_INFO, (uint64_t)i, (uint64_t)&info);
		if (r < 0 || !info.present) continue;
		size_t nlen = strlen(info.name);
		int is_part = 0;
		for (size_t k = 0; k < nlen; k++) {
			if (info.name[k] >= '0' && info.name[k] <= '9') { is_part = 1; break; }
		}
		if (is_part) continue;
		safe_strcpy(out[found].name,  sizeof(out[found].name),  info.name);
		safe_strcpy(out[found].model, sizeof(out[found].model), info.model);
		out[found].sectors    = info.sectors;
		out[found].size_bytes = info.size_bytes;
		found++;
		if (found >= 4) break;
	}
	return found;
}

static int ask_choose_disk(char *out_name, size_t out_cap)
{
	disk_summary_t disks[4];
	int n = list_disks(disks);
	if (n == 0) {
		fputs(C_RED "  No disks detected!" C_RESET "\n", stdout);
		return -1;
	}
	fputs("\n", stdout);
	fputs(C_CYAN "  Available disks:" C_RESET "\n", stdout);
	for (int i = 0; i < n; i++) {
		fputs("    ", stdout);
		putchar((char)('1' + i));
		fputs(") ", stdout);
		fputs(C_BOLD, stdout);
		fputs(disks[i].name, stdout);
		fputs(C_RESET, stdout);
		fputs("  ", stdout);
		uint64_t mb = disks[i].size_bytes / (1024 * 1024);
		char buf[32];
		int bi = 0;
		if (mb == 0) {
			buf[bi++] = '0';
		} else {
			char rev[32];
			int ri = 0;
			uint64_t v = mb;
			while (v) { rev[ri++] = (char)('0' + (v % 10)); v /= 10; }
			while (ri) buf[bi++] = rev[--ri];
		}
		buf[bi] = '\0';
		fputs(buf, stdout);
		fputs(" MB  ", stdout);
		fputs(disks[i].model, stdout);
		fputs("\n", stdout);
	}
	fputs("\n  Select disk [1-", stdout);
	putchar((char)('0' + n));
	fputs("] (q to cancel): ", stdout);
	char c = 0;
	while (1) {
		if (read(0, &c, 1) <= 0) continue;
		if (c >= '1' && c <= (char)('0' + n)) { putchar(c); putchar(10); break; }
		if (c == 'q' || c == 'Q') { putchar(c); putchar(10); return -1; }
	}
	int idx = c - '1';
	safe_strcpy(out_name, out_cap, disks[idx].name);
	return 0;
}

static void progress_done(const char *msg)
{
	putchar('\r');
	fputs(C_GREEN "       ", stdout);
	fputs(msg, stdout);
	fputs(C_RESET "                       \n", stdout);
}

static void progress_fail(const char *msg)
{
	putchar('\r');
	fputs(C_RED "       ", stdout);
	fputs(msg, stdout);
	fputs(C_RESET "                       \n", stdout);
}

static int write_limine_conf(const char *path)
{
	int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) return fd;
	const char *conf =
		"timeout: 5\n"
		"default_entry: 1\n"
		"interface_branding: Cervus\n"
		"wallpaper: boot():/boot/wallpaper.png\n"
		"\n"
		"/Cervus v0.0.2 Alpha\n"
		"    protocol: limine\n"
		"    path: boot():/kernel\n"
		"    module_path: boot():/shell.elf\n"
		"    module_cmdline: init\n";
	write(fd, conf, strlen(conf));
	close(fd);
	return 0;
}

static int do_install(void)
{
	clear_screen();

	fputs(C_CYAN "  Cervus OS Installer" C_RESET "\n", stdout);
	fputs(C_GRAY "  -----------------------------------" C_RESET "\n", stdout);

	char chosen_disk_name[32];
	if (ask_choose_disk(chosen_disk_name, sizeof(chosen_disk_name)) < 0) {
		fputs("\n  Cancelled.\n\n", stdout);
		return 1;
	}

	fputs("\n  Target disk: " C_BOLD, stdout);
	fputs(chosen_disk_name, stdout);
	fputs(C_RESET "\n", stdout);
	fputs("  Layout: ESP (FAT32, 64 MB) + root (ext2) + swap (16 MB)\n", stdout);
	fputs("\n", stdout);
	fputs(C_RED "  WARNING: This will erase ALL data on " C_RESET, stdout);
	fputs(C_BOLD, stdout);
	fputs(chosen_disk_name, stdout);
	fputs(C_RESET C_RED "!" C_RESET "\n\n", stdout);
	fputs("  Continue? [y/n]: ", stdout);

	char c = 0;
	while (1) {
		if (read(0, &c, 1) <= 0) continue;
		if (c == 'y' || c == 'Y' || c == 'n' || c == 'N') { putchar(c); putchar(10); break; }
	}

	if (c == 'n' || c == 'N') {
		fputs("\n  Cancelled.\n\n", stdout);
		return 1;
	}

	disk_summary_t disks[4];
	int n_disks = list_disks(disks);
	uint64_t total_sectors = 0;
	for (int i = 0; i < n_disks; i++) {
		if (strcmp(disks[i].name, chosen_disk_name) == 0) {
			total_sectors = disks[i].sectors;
			break;
		}
	}
	if (total_sectors < 300000) {
		fputs(C_RED "  Disk too small (need at least 150 MB)" C_RESET "\n\n", stdout);
		return 1;
	}

	uint32_t esp_start  = 2048;
	uint32_t esp_size   = 131072;
	uint32_t swap_size  = 32768;
	uint32_t root_start = esp_start + esp_size;
	uint32_t avail      = (uint32_t)total_sectors - root_start - swap_size;
	uint32_t root_size  = avail;
	uint32_t swap_start = root_start + root_size;

	fputs("\n  [1/8] Writing partition table...\n", stdout);
	cervus_mbr_part_t specs[4];
	memset(specs, 0, sizeof(specs));
	specs[0].boot_flag    = 1;
	specs[0].type         = MBR_TYPE_FAT32_LBA;
	specs[0].lba_start    = esp_start;
	specs[0].sector_count = esp_size;
	specs[1].boot_flag    = 0;
	specs[1].type         = MBR_TYPE_LINUX;
	specs[1].lba_start    = root_start;
	specs[1].sector_count = root_size;
	specs[2].boot_flag    = 0;
	specs[2].type         = MBR_TYPE_LINUX_SWAP;
	specs[2].lba_start    = swap_start;
	specs[2].sector_count = swap_size;
	if (cervus_disk_partition(chosen_disk_name, specs, 3) < 0) {
		progress_fail("Failed to write partition table!");
		return 1;
	}
	progress_done("partition table written");

	char part1[32], part2[32];
	snprintf(part1, sizeof(part1), "%s1", chosen_disk_name);
	snprintf(part2, sizeof(part2), "%s2", chosen_disk_name);

	fputs("  [2/8] Formatting ", stdout);
	fputs(part1, stdout);
	fputs(" as FAT32 (ESP)...\n", stdout);
	if (cervus_disk_mkfs_fat32(part1, "CERVUS-ESP") < 0) {
		progress_fail("mkfs.fat32 failed!");
		return 1;
	}
	progress_done("FAT32 ESP created");

	fputs("  [3/8] Formatting ", stdout);
	fputs(part2, stdout);
	fputs(" as ext2 (root)...\n", stdout);
	if (cervus_disk_format(part2, "cervus-root") < 0) {
		progress_fail("mkfs.ext2 failed!");
		return 1;
	}
	progress_done("ext2 root created");

	fputs("  [4/8] Mounting partitions...\n", stdout);
	ensure_dir("/mnt/esp");
	ensure_dir("/mnt/root");
	if (cervus_disk_mount(part1, "/mnt/esp") < 0) {
		progress_fail("mount ESP failed");
		return 1;
	}
	if (cervus_disk_mount(part2, "/mnt/root") < 0) {
		progress_fail("mount root failed");
		cervus_disk_umount("/mnt/esp");
		return 1;
	}
	progress_done("mounted");

	fputs("  [5/8] Copying boot files to ESP...\n", stdout);
	ensure_dir("/mnt/esp/boot");
	ensure_dir("/mnt/esp/boot/limine");
	ensure_dir("/mnt/esp/EFI");
	ensure_dir("/mnt/esp/EFI/BOOT");

	struct { const char *src; const char *dst; int required; } boot_files[] = {
		{ "/boot/kernel",              "/mnt/esp/boot/kernel",                        1 },
		{ "/boot/kernel",              "/mnt/esp/kernel",                             0 },
		{ "/boot/shell.elf",           "/mnt/esp/boot/shell.elf",                     1 },
		{ "/boot/shell.elf",           "/mnt/esp/shell.elf",                          0 },
		{ "/boot/limine-bios.sys",     "/mnt/esp/boot/limine/limine-bios.sys",        0 },
		{ "/boot/limine-bios-hdd.bin", "/mnt/esp/boot/limine/limine-bios-hdd.bin",    0 },
		{ "/boot/BOOTX64.EFI",         "/mnt/esp/EFI/BOOT/BOOTX64.EFI",               0 },
		{ "/boot/BOOTIA32.EFI",        "/mnt/esp/EFI/BOOT/BOOTIA32.EFI",              0 },
		{ "/boot/wallpaper.png",       "/mnt/esp/boot/wallpaper.png",                 0 },
		{ "/boot/wallpaper.png",       "/mnt/esp/wallpaper.png",                      0 },
		{ NULL, NULL, 0 }
	};
	for (int i = 0; boot_files[i].src; i++) {
		struct stat st;
		if (stat(boot_files[i].src, &st) != 0) {
			if (boot_files[i].required) {
				fputs(C_RED "       MISSING required: " C_RESET, stdout);
				fputs(boot_files[i].src, stdout);
				putchar(10);
			} else {
				fputs(C_YELLOW "       skip (missing): " C_RESET, stdout);
				fputs(boot_files[i].src, stdout);
				putchar(10);
			}
			continue;
		}
		if (copy_one_file_progress(boot_files[i].src, boot_files[i].dst, boot_files[i].src) < 0) {
			fputs(C_RED "       FAILED: " C_RESET, stdout);
			fputs(boot_files[i].dst, stdout);
			putchar(10);
		} else {
			fputs(C_GREEN "       " C_RESET, stdout);
			fputs(boot_files[i].dst, stdout);
			putchar(10);
		}
	}

	fputs("  [6/8] Writing limine.conf...\n", stdout);
	int ok1 = write_limine_conf("/mnt/esp/boot/limine/limine.conf");
	int ok2 = write_limine_conf("/mnt/esp/EFI/BOOT/limine.conf");
	int ok3 = write_limine_conf("/mnt/esp/limine.conf");
	if (ok1 < 0 && ok2 < 0 && ok3 < 0)
		progress_fail("failed to write limine.conf");
	else
		progress_done("limine.conf written (3 locations)");

	fputs("  [7/8] Populating root filesystem...\n", stdout);
	const char *rdirs[] = {
		"/mnt/root/bin", "/mnt/root/apps", "/mnt/root/etc",
		"/mnt/root/home", "/mnt/root/tmp", "/mnt/root/var",
		"/mnt/root/usr",
		NULL
	};
	for (int i = 0; rdirs[i]; i++) ensure_dir(rdirs[i]);

	fputs("       copying /bin...\n", stdout);
	copy_tree("/bin",  "/mnt/root/bin");
	fputs("       copying /apps...\n", stdout);
	copy_tree("/apps", "/mnt/root/apps");

	struct stat ust;
	if (stat("/usr", &ust) == 0) {
		fputs("       copying /usr (sysroot)...\n", stdout);
		copy_tree("/usr", "/mnt/root/usr");
	} else {
		fputs("       /usr not present in live image — sysroot skipped\n", stdout);
	}

	struct stat est;
	if (stat("/etc", &est) == 0) {
		fputs("       copying /etc...\n", stdout);
		static dir_entry_t etc_entries[MAX_ENTRIES];
		int nn = read_dir_entries("/etc", etc_entries, MAX_ENTRIES);
		for (int i = 0; i < nn; i++) {
			const char *nm = etc_entries[i].name;
			size_t nl = strlen(nm);
			int is_txt = (nl >= 5 && strcmp(nm + nl - 4, ".txt") == 0);
			if (etc_entries[i].type == 0) {
				char sp[256], dp[256];
				path_join("/etc", nm, sp, sizeof(sp));
				if (is_txt) path_join("/mnt/root/home", nm, dp, sizeof(dp));
				else        path_join("/mnt/root/etc",  nm, dp, sizeof(dp));
				copy_one_file(sp, dp);
			} else if (etc_entries[i].type == 1) {
				char sp[256], dp[256];
				path_join("/etc", nm, sp, sizeof(sp));
				path_join("/mnt/root/etc", nm, dp, sizeof(dp));
				copy_tree(sp, dp);
			}
		}
	}

	struct stat hst;
	if (stat("/home", &hst) == 0) {
		fputs("       copying /home...\n", stdout);
		copy_tree("/home", "/mnt/root/home");
	}

	fputs("  [8/8] Installing BIOS stage1 to MBR...\n", stdout);
	{
		static uint8_t sys_buf[300 * 1024];
		int fd = open("/mnt/esp/boot/limine/limine-bios-hdd.bin", O_RDONLY, 0);
		if (fd < 0) {
			progress_fail("limine-bios-hdd.bin not found on ESP");
		} else {
			struct stat st;
			int sr = stat("/mnt/esp/boot/limine/limine-bios-hdd.bin", &st);
			uint32_t sys_size = (sr == 0) ? (uint32_t)st.st_size : 0;

			if (sys_size == 0 || sys_size > sizeof(sys_buf)) {
				close(fd);
				progress_fail("bad limine-bios-hdd.bin size");
			} else {
				uint32_t got = 0;
				int ok = 1;
				while (got < sys_size) {
					ssize_t n = read(fd, sys_buf + got, sys_size - got);
					if (n <= 0) { ok = 0; break; }
					got += (uint32_t)n;
				}
				close(fd);
				if (!ok || got != sys_size) {
					progress_fail("short read on limine-bios-hdd.bin");
				} else {
					long r = cervus_disk_bios_install(chosen_disk_name, sys_buf, sys_size);
					if (r < 0) progress_fail("BIOS install syscall failed");
					else       progress_done("BIOS stage1 installed");
				}
			}
		}
	}

	cervus_disk_umount("/mnt/esp");
	cervus_disk_umount("/mnt/root");

	clear_screen();

	fputs("\n" C_GREEN "  Installation complete!" C_RESET "\n", stdout);
	fputs("  The system will reboot in 3 seconds.\n\n", stdout);

	for (int i = 3; i > 0; i--) {
		fputs("  Rebooting in ", stdout);
		putchar((char)('0' + i));
		fputs("...\r", stdout);
		syscall1(SYS_SLEEP_NS, 1000000000ULL);
	}
	fputs("\n", stdout);
	syscall0(SYS_REBOOT);
	return 0;
}

int main(int argc, char **argv)
{
	const char *mode = getenv_argv(argc, argv, "MODE", "");

	if (strcmp(mode, "live") != 0) {
		fputs(C_RED "install-on-disk: this command is only available in Live mode.\n" C_RESET, stderr);
		fputs("The system is already installed on disk.\n", stderr);
		return 1;
	}

	do_install();
	return 0;
}