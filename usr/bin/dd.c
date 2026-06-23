#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/cervus.h>
#include <cervus_util.h>

static const char USAGE[] =
    "Usage: dd [if=FILE] [of=FILE] [bs=N] [count=N] [skip=N] [seek=N] [status=none]\nCopy a file, converting and formatting according to the operands.\n\n  if=F     read from F (default stdin)\n  of=F     write to F (default stdout)\n  bs=N     read/write N bytes at a time (default 512; suffixes K M G)\n  count=N  copy only N input blocks\n  skip=N   skip N input blocks\n  seek=N   skip N output blocks\n";

static unsigned long long parse_sz(const char *s)
{
    char *end;
    unsigned long long v = strtoull(s, &end, 10);
    if (*end == 'K' || *end == 'k') v <<= 10;
    else if (*end == 'M' || *end == 'm') v <<= 20;
    else if (*end == 'G' || *end == 'g') v <<= 30;
    return v;
}

int main(int argc, char **argv)
{
    if (cervus_check_help_version(argc, argv, USAGE, "dd")) return 0;

    const char *inf = NULL, *outf = NULL;
    unsigned long long bs = 512, count = ~0ULL, skip = 0, seek = 0;
    int quiet = 0;

    for (int i = 1; i < argc; i++) {
        if      (!strncmp(argv[i], "if=", 3))     inf  = argv[i] + 3;
        else if (!strncmp(argv[i], "of=", 3))     outf = argv[i] + 3;
        else if (!strncmp(argv[i], "bs=", 3))     bs    = parse_sz(argv[i] + 3);
        else if (!strncmp(argv[i], "count=", 6))  count = parse_sz(argv[i] + 6);
        else if (!strncmp(argv[i], "skip=", 5))   skip  = parse_sz(argv[i] + 5);
        else if (!strncmp(argv[i], "seek=", 5))   seek  = parse_sz(argv[i] + 5);
        else if (!strcmp(argv[i], "status=none")) quiet = 1;
        else { fputs(USAGE, stderr); return 1; }
    }
    if (bs == 0 || bs > 16*1024*1024) { fputs("dd: invalid bs\n", stderr); return 1; }

    int ifd = 0, ofd = 1;
    if (inf) {
        ifd = open(inf, O_RDONLY);
        if (ifd < 0) { fprintf(stderr, "dd: cannot open '%s'\n", inf); return 1; }
    }
    if (outf) {
        ofd = open(outf, O_WRONLY | O_CREAT, 0644);
        if (ofd < 0) { fprintf(stderr, "dd: cannot open '%s'\n", outf); return 1; }
    }

    char *buf = malloc(bs);
    if (!buf) { fputs("dd: out of memory\n", stderr); return 1; }

    if (skip) lseek(ifd, (off_t)(skip * bs), SEEK_SET);
    if (seek) lseek(ofd, (off_t)(seek * bs), SEEK_SET);

    uint64_t t0 = cervus_uptime_ns();
    unsigned long long full = 0, partial = 0, bytes = 0;

    while (full + partial < count) {
        ssize_t r = read(ifd, buf, bs);
        if (r < 0) { fputs("dd: read error\n", stderr); break; }
        if (r == 0) break;

        ssize_t off = 0;
        while (off < r) {
            ssize_t w = write(ofd, buf + off, (size_t)(r - off));
            if (w <= 0) { fputs("dd: write error\n", stderr); goto out;
            }
            off += w;
        }
        bytes += (unsigned long long)r;
        if ((unsigned long long)r == bs) full++;
        else partial++;
    }
out:
    free(buf);
    if (inf)  close(ifd);
    if (outf) { fsync(ofd); close(ofd); }

    if (!quiet) {
        uint64_t dt = cervus_uptime_ns() - t0;
        unsigned long long ms = dt / 1000000ULL;
        if (ms == 0) ms = 1;
        unsigned long long kbps = bytes / ms;
        fprintf(stderr, "%llu+%llu records in/out, %llu bytes copied, %llu.%03llu s, %llu.%llu MB/s\n",
                full, partial, bytes,
                ms / 1000, ms % 1000,
                kbps / 1000, (kbps % 1000) / 100);
    }
    return 0;
}
