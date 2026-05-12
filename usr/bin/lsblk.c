#include <stdio.h>
#include <string.h>
#include <sys/cervus.h>

static const cervus_mount_info_t *find_mount(const cervus_mount_info_t *m, int n, const char *dev)
{
    for (int i = 0; i < n; i++)
        if (strcmp(m[i].device, dev) == 0) return &m[i];
    return NULL;
}

static int name_is_partition(const char *name)
{
    for (size_t k = 0; name[k]; k++)
        if (name[k] >= '0' && name[k] <= '9') return 1;
    return 0;
}

static int print_size_human(char *buf, size_t bufsz, uint64_t bytes)
{
    uint64_t mb = bytes / (1024 * 1024);
    if (mb >= 1024) {
        return snprintf(buf, bufsz, "%lu.%luG",
                        (unsigned long)(mb / 1024),
                        (unsigned long)((mb % 1024) * 10 / 1024));
    } else if (mb > 0) {
        return snprintf(buf, bufsz, "%luM", (unsigned long)mb);
    } else {
        return snprintf(buf, bufsz, "%luK", (unsigned long)(bytes / 1024));
    }
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    cervus_mount_info_t mounts[16];
    long nm = cervus_list_mounts(mounts, 16);
    if (nm < 0) nm = 0;

    fputs("NAME    SIZE     TYPE      MOUNTPOINT\n", stdout);
    fputs("------  -------  --------  -----------------------\n", stdout);

    int found = 0;
    for (int i = 0; i < 8; i++) {
        cervus_disk_info_t info;
        memset(&info, 0, sizeof(info));
        int r = cervus_disk_info(i, &info);
        if (r < 0) break;
        if (!info.present) continue;
        found++;

        char sz[16];
        print_size_human(sz, sizeof(sz), info.size_bytes);

        int is_part = name_is_partition(info.name);
        const cervus_mount_info_t *m = is_part
            ? find_mount(mounts, (int)nm, info.name)
            : NULL;
        const char *type_str = !is_part ? "disk" : (m ? m->fstype : "part");

        printf("%-6s  %-7s  %-8s  %s\n",
               info.name, sz, type_str, m ? m->path : "");
    }

    for (long i = 0; i < nm; i++) {
        const char *dev = mounts[i].device;
        if (name_is_partition(dev)) continue;
        if (strcmp(dev, "ramfs") != 0 && strcmp(dev, "devfs") != 0) continue;
        printf("%-6s  %-7s  %-8s  %s\n",
               dev, "-", mounts[i].fstype, mounts[i].path);
        found++;
    }

    if (!found) fputs("  (no disks detected)\n", stdout);
    return 0;
}
