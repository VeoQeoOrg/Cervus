#include <stdio.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <cervus_util.h>

int main(int argc, char **argv)
{
    const char *cwd = get_cwd_flag(argc, argv);
    const char *path = NULL;
    long skip = 0;
    long count = -1;

    for (int i = 1; i < argc; i++) {
        if (is_shell_flag(argv[i])) continue;
        if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            skip = strtol(argv[++i], NULL, 0);
            continue;
        }
        if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
            count = strtol(argv[++i], NULL, 0);
            continue;
        }
        if (!path) path = argv[i];
    }

    if (!path) {
        fputs("Usage: hexdump [-s OFF] [-n CNT] <file>\n", stdout);
        return 1;
    }

    char resolved[512];
    resolve_path(cwd, path, resolved, sizeof(resolved));

    struct stat st;
    if (stat(resolved, &st) == 0 && st.st_type == DT_DIR) {
        fprintf(stderr, "hexdump: %s: Is a directory\n", path);
        return 1;
    }

    int fd = open(resolved, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "hexdump: cannot open: %s\n", path);
        return 1;
    }

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
            if (i < n) printf("%02x ", buf[i]);
            else       fputs("   ", stdout);
        }
        putchar(' ');
        for (int i = 8; i < 16; i++) {
            if (i < n) printf("%02x ", buf[i]);
            else       fputs("   ", stdout);
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
    close(fd);
    return 0;
}