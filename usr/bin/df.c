#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/cervus.h>
#include <cervus_util.h>

static const char USAGE[] =
    "Usage: df [-h]\nReport file system disk space usage.\n\n  -h   human-readable sizes\n";

static int g_human;

static void fmt(uint64_t bytes, char *out, int max)
{
    if (g_human) {
        if      (bytes >= 1024ULL*1024*1024) snprintf(out, max, "%llu.%lluG", (unsigned long long)(bytes >> 30), (unsigned long long)((bytes >> 20) % 1024 * 10 / 1024));
        else if (bytes >= 1024ULL*1024)      snprintf(out, max, "%lluM", (unsigned long long)(bytes >> 20));
        else                                  snprintf(out, max, "%lluK", (unsigned long long)(bytes >> 10));
    } else {
        snprintf(out, max, "%llu", (unsigned long long)(bytes / 1024));
    }
}

int main(int argc, char **argv)
{
    if (cervus_check_help_version(argc, argv, USAGE, "df")) return 0;

    int opt;
    while ((opt = getopt(argc, argv, "h")) != -1) {
        if (opt == 'h') g_human = 1;
        else { fputs(USAGE, stderr); return 1; }
    }

    FILE *m = fopen("/proc/mounts", "r");
    if (!m) { fputs("df: cannot read /proc/mounts\n", stderr); return 1; }

    printf("%-16s %-8s %10s %10s %10s %5s %s\n",
           "Filesystem", "Type", g_human ? "Size" : "1K-blocks",
           "Used", "Avail", "Use%", "Mounted on");

    char line[512];
    while (fgets(line, sizeof(line), m)) {
        char dev[128], path[256], fstype[64];
        if (sscanf(line, "%127s %255s %63s", dev, path, fstype) != 3) continue;

        cervus_statvfs_t sv;
        if (cervus_statvfs(path, &sv) == 0 && sv.f_blocks > 0) {
            uint64_t total = sv.f_blocks * sv.f_bsize;
            uint64_t avail = sv.f_bavail * sv.f_bsize;
            uint64_t used  = total - sv.f_bfree * sv.f_bsize;
            unsigned pct = total ? (unsigned)((used * 100 + total - 1) / total) : 0;
            char st[32], su[32], sa[32];
            fmt(total, st, sizeof(st));
            fmt(used,  su, sizeof(su));
            fmt(avail, sa, sizeof(sa));
            printf("%-16s %-8s %10s %10s %10s %4u%% %s\n",
                   dev, fstype, st, su, sa, pct, path);
        } else {
            printf("%-16s %-8s %10s %10s %10s %5s %s\n",
                   dev, fstype, "-", "-", "-", "-", path);
        }
    }
    fclose(m);
    return 0;
}
