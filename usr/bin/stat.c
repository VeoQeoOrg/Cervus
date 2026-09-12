#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <time.h>
#include <cervus_util.h>

static const char *type_str(uint32_t t)
{
    switch (t) {
        case 0: return "regular file";
        case 1: return "directory";
        case 2: return "char device";
        case 3: return "block device";
        case 4: return "symlink";
        case 5: return "pipe";
        default: return "unknown";
    }
}

static void fmt_perms(mode_t m, uint32_t type, char *buf)
{
    buf[0] = (type == 1 || S_ISDIR(m)) ? 'd'
           : S_ISLNK(m) ? 'l' : S_ISCHR(m) ? 'c'
           : S_ISBLK(m) ? 'b' : S_ISFIFO(m) ? 'p' : '-';
    const char *bits = "rwxrwxrwx";
    for (int i = 0; i < 9; i++)
        buf[1 + i] = (m & (0400 >> i)) ? bits[i] : '-';
    buf[10] = '\0';
}

static const char *type_short(uint32_t t)
{
    switch (t) {
        case 0: return "regular";
        case 1: return "directory";
        case 2: return "char";
        case 3: return "block";
        case 4: return "symlink";
        case 5: return "pipe";
        default: return "unknown";
    }
}

static void fmt_stamp(int64_t t, char *out, size_t cap)
{
    if (t <= 0) { snprintf(out, cap, "-"); return; }
    time_t tv = (time_t)t;
    struct tm *tm = localtime(&tv);
    if (!tm) { snprintf(out, cap, "-"); return; }
    snprintf(out, cap, "%04d-%02d-%02d %02d:%02d:%02d",
             tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
             tm->tm_hour, tm->tm_min, tm->tm_sec);
}

static const char USAGE[] =
    "Usage: stat [-t] file ...\nDisplay file or file system status.\n\n  -t   terse output\n";

static void usage(void) { fputs(USAGE, stderr); }

int main(int argc, char **argv)
{
    if (cervus_check_help_version(argc, argv, USAGE, "stat")) return 0;
    argc = cervus_end_of_options(argc, argv);

    int terse = 0;
    int opt;
    while ((opt = getopt(argc, argv, "t")) != -1) {
        switch (opt) {
            case 't': terse = 1; break;
            default: usage(); return 1;
        }
    }
    if (optind >= argc) { usage(); return 1; }

    int rc = 0;
    for (int i = optind; i < argc; i++) {
        char resolved[512];
        snprintf(resolved, sizeof(resolved), "%s", argv[i]);
        struct stat st;
        if (stat(resolved, &st) < 0) {
            fprintf(stderr, "stat: cannot stat '%s'\n", argv[i]);
            rc = 1; continue;
        }
        char perms[12];
        fmt_perms(st.st_mode, st.st_type, perms);
        if (terse) {
            printf("%s %lu %lu %04o %u %u %s\n",
                   argv[i],
                   (unsigned long)st.st_size,
                   (unsigned long)st.st_ino,
                   (unsigned)(st.st_mode & 07777),
                   (unsigned)st.st_uid,
                   (unsigned)st.st_gid,
                   type_short(st.st_type));
        } else {
            printf("  File:   %s\n", argv[i]);
            printf("  Type:   %s\n", type_str(st.st_type));
            printf("  Access: (%04o/%s)\n", (unsigned)(st.st_mode & 07777), perms);
            printf("  Inode:  0x%lx\n", (unsigned long)st.st_ino);
            printf("  Size:   %lu bytes\n", (unsigned long)st.st_size);
            printf("  Blocks: %lu\n", (unsigned long)st.st_blocks);
            printf("  UID:    %u\n", (unsigned)st.st_uid);
            printf("  GID:    %u\n", (unsigned)st.st_gid);
            char ts[40];
            fmt_stamp(st.st_atime, ts, sizeof ts); printf("  Access: %s\n", ts);
            fmt_stamp(st.st_mtime, ts, sizeof ts); printf("  Modify: %s\n", ts);
            fmt_stamp(st.st_ctime, ts, sizeof ts); printf("  Change: %s\n", ts);
            if (i + 1 < argc) putchar('\n');
        }
    }
    return rc;
}
