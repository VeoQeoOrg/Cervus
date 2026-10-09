#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/stat.h>
#include <cervus_util.h>

static const char USAGE[] =
    "Usage: truncate OPTION... FILE...\n"
    "Shrink or extend the size of each FILE to the specified size\n\n"
    "  -c, --no-create        do not create any files\n"
    "  -o, --io-blocks        treat SIZE as number of IO blocks instead of bytes\n"
    "  -r, --reference=RFILE  base size on RFILE\n"
    "  -s, --size=SIZE        set or adjust the file size by SIZE bytes\n\n"
    "SIZE is an integer with an optional unit: K,M,G,T,P,E (powers of 1024)\n"
    "or KB,MB,... (powers of 1000). Binary prefixes can be used too: KiB=K, MiB=M.\n"
    "SIZE may be prefixed by '+' extend by, '-' reduce by, '<' at most,\n"
    "'>' at least, '/' round down to multiple of, '%' round up to multiple of.\n";

static int parse_size(const char *s, char *op, long long *out)
{
    *op = 0;
    while (*s == ' ') s++;
    if (*s && strchr("+-<>/%", *s)) { *op = *s++; while (*s == ' ') s++; }
    if (*s < '0' || *s > '9') return -1;
    char *end;
    errno = 0;
    unsigned long long v = strtoull(s, &end, 10);
    if (errno) return -1;
    static const char units[] = "KMGTPE";
    if (*end) {
        const char *u = strchr(units, end[0] == 'k' ? 'K' : end[0]);
        if (!u || !*u) return -1;
        int power = (int)(u - units) + 1;
        unsigned long long base = 1024;
        if (end[1] == 'B' && !end[2]) base = 1000;
        else if (end[1] == 'i' && end[2] == 'B' && !end[3]) base = 1024;
        else if (end[1]) return -1;
        for (int i = 0; i < power; i++) {
            if (v > (unsigned long long)0x7fffffffffffffffLL / base) return -1;
            v *= base;
        }
    }
    if (v > 0x7fffffffffffffffULL) return -1;
    *out = (long long)v;
    return 0;
}

int main(int argc, char **argv)
{
    if (cervus_check_help_version(argc, argv, USAGE, "truncate")) return 0;
    static const struct option longopts[] = {
        { "no-create", no_argument,       0, 'c' },
        { "io-blocks", no_argument,       0, 'o' },
        { "reference", required_argument, 0, 'r' },
        { "size",      required_argument, 0, 's' },
        { 0, 0, 0, 0 }
    };
    int nocreate = 0, blocks = 0, opt;
    const char *ref = NULL, *sizearg = NULL;
    while ((opt = getopt_long(argc, argv, "cor:s:", longopts, NULL)) != -1) {
        switch (opt) {
        case 'c': nocreate = 1; break;
        case 'o': blocks = 1; break;
        case 'r': ref = optarg; break;
        case 's': sizearg = optarg; break;
        default: fputs(USAGE, stderr); return 1;
        }
    }
    if (!ref && !sizearg) { fputs("truncate: you must specify either --size or --reference\n", stderr); return 1; }
    if (optind >= argc) { fputs("truncate: missing file operand\n", stderr); return 1; }

    char op = 0;
    long long size = 0;
    if (sizearg && parse_size(sizearg, &op, &size) < 0) {
        fprintf(stderr, "truncate: invalid number: '%s'\n", sizearg);
        return 1;
    }
    if (ref && op == 0 && sizearg) {
        fputs("truncate: you must specify a relative --size with --reference\n", stderr);
        return 1;
    }
    if ((op == '/' || op == '%') && size == 0) {
        fputs("truncate: division by zero\n", stderr);
        return 1;
    }
    long long refsize = -1;
    if (ref) {
        struct stat st;
        if (stat(ref, &st) < 0) { fprintf(stderr, "truncate: cannot stat '%s': %s\n", ref, strerror(errno)); return 1; }
        refsize = st.st_size;
    }

    int rc = 0;
    for (int i = optind; i < argc; i++) {
        int fd = open(argv[i], O_WRONLY | (nocreate ? 0 : O_CREAT), 0666);
        if (fd < 0) {
            if (nocreate && errno == ENOENT) continue;
            fprintf(stderr, "truncate: cannot open '%s' for writing: %s\n", argv[i], strerror(errno));
            rc = 1;
            continue;
        }
        struct stat st;
        if (fstat(fd, &st) < 0) { rc = 1; close(fd); continue; }
        long long amount = size;
        if (blocks) amount *= st.st_blksize > 0 ? st.st_blksize : 512;
        long long cur = refsize >= 0 ? refsize : st.st_size;
        long long n;
        switch (op) {
        case '+': n = cur + amount; break;
        case '-': n = cur - amount; if (n < 0) n = 0; break;
        case '<': n = cur < amount ? cur : amount; break;
        case '>': n = cur > amount ? cur : amount; break;
        case '/': n = cur / amount * amount; break;
        case '%': n = (cur + amount - 1) / amount * amount; break;
        default:  n = refsize >= 0 && !sizearg ? refsize : amount; break;
        }
        if (ftruncate(fd, n) < 0) {
            fprintf(stderr, "truncate: failed to truncate '%s' at %lld bytes: %s\n", argv[i], n, strerror(errno));
            rc = 1;
        }
        close(fd);
    }
    return rc;
}
