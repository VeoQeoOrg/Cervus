#include "cervus_user.h"

#define SYS_DISK_INFO    533
#define SYS_LIST_MOUNTS  546

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

static inline int disk_info(int index, disk_info_t *info)
{
	return (int)syscall2(SYS_DISK_INFO, index, info);
}

static inline long list_mounts_sc(mount_info_t *out, int max)
{
	return syscall2(SYS_LIST_MOUNTS, (long)out, (long)max);
}

static const mount_info_t *find_mount(const mount_info_t *mounts, int n, const char *devname)
{
	for (int i = 0; i < n; i++) {
		if (strcmp(mounts[i].device, devname) == 0) return &mounts[i];
	}
	return NULL;
}

static void print_size_human(uint64_t bytes)
{
	uint64_t mb = bytes / (1024 * 1024);
	if (mb >= 1024) {
		print_u64(mb / 1024);
		ws(".");
		print_u64((mb % 1024) * 10 / 1024);
		ws("G");
	} else if (mb > 0) {
		print_u64(mb);
		ws("M");
	} else {
		print_u64(bytes / 1024);
		ws("K");
	}
}

static int print_size_human_len(uint64_t bytes)
{
	uint64_t mb = bytes / (1024 * 1024);
	if (mb >= 1024) {
		int w = 0;
		uint64_t gb = mb / 1024;
		if (gb == 0) w = 1;
		else { uint64_t t = gb; while (t) { w++; t /= 10; } }
		return w + 3;
	} else if (mb > 0) {
		int w = 0;
		uint64_t t = mb;
		while (t) { w++; t /= 10; }
		return w + 1;
	} else {
		uint64_t kb = bytes / 1024;
		int w = 0;
		uint64_t t = kb;
		if (t == 0) w = 1;
		else while (t) { w++; t /= 10; }
		return w + 1;
	}
}

static void pad_to(int current, int target)
{
	for (int i = current; i < target; i++) wc(' ');
}

static int name_is_partition(const char *name)
{
	for (size_t k = 0; name[k]; k++) {
		if (name[k] >= '0' && name[k] <= '9') return 1;
	}
	return 0;
}

CERVUS_MAIN(main)
{
	(void)argc;
	(void)argv;

	mount_info_t mounts[16];
	long nm = list_mounts_sc(mounts, 16);
	if (nm < 0) nm = 0;

	ws("NAME    SIZE     TYPE      MOUNTPOINT\n");
	ws("------  -------  --------  -----------------------\n");

	int found = 0;
	for (int i = 0; i < 8; i++) {
		disk_info_t info;
		memset(&info, 0, sizeof(info));
		int r = disk_info(i, &info);
		if (r < 0) break;
		if (!info.present) continue;
		found++;

		ws(info.name);
		pad_to((int)strlen(info.name), 8);

		int szlen = print_size_human_len(info.size_bytes);
		print_size_human(info.size_bytes);
		pad_to(szlen, 9);

		int is_part = name_is_partition(info.name);
		const mount_info_t *m = is_part ? find_mount(mounts, (int)nm, info.name) : NULL;

		const char *type_str;
		if (!is_part)    type_str = "disk";
		else if (m)      type_str = m->fstype;
		else             type_str = "part";

		ws(type_str);
		pad_to((int)strlen(type_str), 10);

		if (m) ws(m->path);
		wn();
	}

	for (long i = 0; i < nm; i++) {
		const char *dev = mounts[i].device;
		if (name_is_partition(dev)) continue;
		if (strcmp(dev, "ramfs") != 0 && strcmp(dev, "devfs") != 0) continue;

		ws(dev);
		pad_to((int)strlen(dev), 8);
		ws("-");
		pad_to(1, 9);
		ws(mounts[i].fstype);
		pad_to((int)strlen(mounts[i].fstype), 10);
		ws(mounts[i].path);
		wn();
		found++;
	}

	if (!found) ws("  (no disks detected)\n");
}