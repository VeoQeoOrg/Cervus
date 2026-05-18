#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <stdint.h>
#include <cervus_util.h>

static const char USAGE[] =
    "Usage: hexdump [-C] [-n length] [-s skip] [file ...]\nDisplay file contents in hex+ASCII canonical form.\n\n  -C       canonical (default)\n  -n N     display only N bytes\n  -s OFF   skip OFF bytes at start\n  -v       no compression (no-op)\n";

static void usage(void) { fputs(USAGE, stderr); }

static int dump_fd(int fd, long skip, long count)
{
    if (skip > 0) lseek(fd, skip, 0);
    uint8_t buf[16];
    uint64_t offset = (uint64_t)skip;
    long remaining = count;
    ssize_t n;
    while ((n = read(fd, buf, 16)) > 0) {
        if (remaining >= 0) {
            if (remaining == 0) break;
            if (n > remaining) n = remaining;
            remaining -= n;
        }
        printf("%016lx  ", (unsigned long)offset);
        for (int i = 0; i < 8; i++) {
            if (i < n) printf("%02x ", buf[i]); else fputs("   ", stdout);
        }
        putchar(' ');
        for (int i = 8; i < 16; i++) {
            if (i < n) printf("%02x ", buf[i]); else fputs("   ", stdout);
        }
        fputs(" |", stdout);
        for (int i = 0; i < n; i++) {
            char c = isprint(buf[i]) ? (char)buf[i] : '.';
            putchar(c);
        }
        fputs("|\n", stdout);
        offset += (uint64_t)n;
    }
    printf("%016lx\n", (unsigned long)offset);
    return 0;
}

int main(int argc, char **argv)
{
    if (cervus_check_help_version(argc, argv, USAGE, "hexdump")) return 0;
    const char *cwd = get_cwd_flag(argc, argv);
    argc = cervus_filter_args(argc, argv);

    long skip = 0;
    long count = -1;
    int opt;
    while ((opt = getopt(argc, argv, "Cn:s:v")) != -1) {
        switch (opt) {
            case 'C': break;
            case 'n': count = strtol(optarg, NULL, 0); break;
            case 's': skip  = strtol(optarg, NULL, 0); break;
            case 'v': break;
            default: usage(); return 1;
        }
    }

    if (optind >= argc) return dump_fd(0, skip, count);

    int rc = 0;
    for (int i = optind; i < argc; i++) {
        char resolved[512];
        resolve_path(cwd, argv[i], resolved, sizeof(resolved));
        struct stat st;
        if (stat(resolved, &st) == 0 && st.st_type == DT_DIR) {
            fprintf(stderr, "hexdump: %s: is a directory\n", argv[i]);
            rc = 1; continue;
        }
        int fd = open(resolved, O_RDONLY);
        if (fd < 0) { fprintf(stderr, "hexdump: cannot open '%s'\n", argv[i]); rc = 1; continue; }
        dump_fd(fd, skip, count);
        close(fd);
    }
    return rc;
}
