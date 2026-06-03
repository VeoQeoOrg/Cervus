#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
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

static const char USAGE[] =
    "Usage: stat [-t] file ...\nDisplay file or file system status.\n\n  -t   terse output\n";

static void usage(void) { fputs(USAGE, stderr); }

int main(int argc, char **argv)
{
    if (cervus_check_help_version(argc, argv, USAGE, "stat")) return 0;

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
        if (terse) {
            printf("%s %lu %lu %u %u %s\n",
                   argv[i],
                   (unsigned long)st.st_size,
                   (unsigned long)st.st_ino,
                   (unsigned)st.st_uid,
                   (unsigned)st.st_gid,
                   type_short(st.st_type));
        } else {
            printf("  File:   %s\n", argv[i]);
            printf("  Type:   %s\n", type_str(st.st_type));
            printf("  Inode:  0x%lx\n", (unsigned long)st.st_ino);
            printf("  Size:   %lu bytes\n", (unsigned long)st.st_size);
            printf("  Blocks: %lu\n", (unsigned long)st.st_blocks);
            printf("  UID:    %u\n", (unsigned)st.st_uid);
            printf("  GID:    %u\n", (unsigned)st.st_gid);
            if (i + 1 < argc) putchar('\n');
        }
    }
    return rc;
}
