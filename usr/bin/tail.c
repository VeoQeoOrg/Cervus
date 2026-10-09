#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <getopt.h>
#include <sys/stat.h>
#include <cervus_util.h>

static const char USAGE[] =
    "Usage: tail [OPTION]... [FILE]...\n"
    "Print the last 10 lines of each FILE to standard output.\n"
    "With more than one FILE, precede each with a header giving the file name.\n"
    "With no FILE, or when FILE is -, read standard input.\n\n"
    "  -c, --bytes=[+]NUM   output the last NUM bytes; or use -c +NUM to output\n"
    "                       starting with byte NUM of each file\n"
    "  -f, --follow         output appended data as the file grows\n"
    "  -n, --lines=[+]NUM   output the last NUM lines, instead of the last 10;\n"
    "                       or use -n +NUM to skip NUM-1 lines at the start\n"
    "  -q, --quiet          never output headers giving file names\n"
    "  -s, --sleep-interval=N  with -f, sleep for about N seconds between iterations\n"
    "  -v, --verbose        always output headers giving file names\n";

static int g_bytes;
static int g_from_start;
static unsigned long long g_count = 10;
static double g_sleep = 1.0;

static int parse_count(const char *s)
{
    g_from_start = 0;
    if (*s == '+') { g_from_start = 1; s++; }
    else if (*s == '-') s++;
    char *end;
    errno = 0;
    unsigned long long v = strtoull(s, &end, 10);
    if (errno || end == s) return -1;
    unsigned long long mul = 1;
    if (*end) {
        switch (*end) {
        case 'b': mul = 512; break;
        case 'k': case 'K': mul = 1024; break;
        case 'M': mul = 1024ULL * 1024; break;
        case 'G': mul = 1024ULL * 1024 * 1024; break;
        default: return -1;
        }
        if (end[1] == 'B' && !end[2]) mul = mul == 512 ? 512 : (mul == 1024 ? 1000 : mul == 1024ULL * 1024 ? 1000000ULL : 1000000000ULL);
        else if (end[1]) return -1;
    }
    g_count = v * mul;
    return 0;
}

static void out(const char *p, size_t n)
{
    fwrite(p, 1, n, stdout);
}

static int copy_rest(int fd)
{
    char buf[65536];
    ssize_t n;
    while ((n = read(fd, buf, sizeof buf)) > 0) out(buf, (size_t)n);
    return n < 0 ? -1 : 0;
}

static int skip_start(int fd)
{
    if (g_count <= 1) return copy_rest(fd);
    unsigned long long skip = g_count - 1;
    char buf[65536];
    ssize_t n;
    while ((n = read(fd, buf, sizeof buf)) > 0) {
        size_t i = 0;
        if (g_bytes) {
            if ((unsigned long long)n <= skip) { skip -= (unsigned long long)n; continue; }
            i = (size_t)skip;
            skip = 0;
        } else {
            while (i < (size_t)n && skip) if (buf[i++] == '\n') skip--;
            if (skip) continue;
        }
        out(buf + i, (size_t)n - i);
        return copy_rest(fd);
    }
    return n < 0 ? -1 : 0;
}

static int tail_seekable(int fd, off_t size)
{
    off_t start;
    if (g_bytes) {
        start = (unsigned long long)size > g_count ? size - (off_t)g_count : 0;
    } else {
        if (g_count == 0) return 0;
        char buf[65536];
        off_t pos = size;
        unsigned long long lines = 0;
        int first = 1;
        start = 0;
        while (pos > 0) {
            size_t chunk = pos > (off_t)sizeof buf ? sizeof buf : (size_t)pos;
            pos -= (off_t)chunk;
            if (pread(fd, buf, chunk, pos) != (ssize_t)chunk) return -1;
            for (size_t i = chunk; i > 0; i--) {
                if (buf[i - 1] != '\n') { first = 0; continue; }
                if (first) { first = 0; continue; }
                if (++lines == g_count) { start = pos + (off_t)i; goto found; }
            }
        }
    }
found:
    if (lseek(fd, start, SEEK_SET) < 0) return -1;
    return copy_rest(fd);
}

static int tail_stream(int fd)
{
    size_t cap = 65536, len = 0;
    char *buf = malloc(cap);
    if (!buf) return -1;
    char chunk[65536];
    ssize_t n;
    while ((n = read(fd, chunk, sizeof chunk)) > 0) {
        if (len + (size_t)n > cap) {
            size_t ncap = cap * 2;
            while (len + (size_t)n > ncap) ncap *= 2;
            char *nb = realloc(buf, ncap);
            if (!nb) { free(buf); return -1; }
            buf = nb;
            cap = ncap;
        }
        memcpy(buf + len, chunk, (size_t)n);
        len += (size_t)n;
        if (g_bytes && len > g_count * 2 && len > 1 << 20) {
            size_t keep = (size_t)g_count;
            memmove(buf, buf + len - keep, keep);
            len = keep;
        }
    }
    size_t start;
    if (g_bytes) {
        start = len > g_count ? len - (size_t)g_count : 0;
    } else {
        unsigned long long lines = 0;
        start = 0;
        size_t i = len;
        if (i && buf[i - 1] == '\n') i--;
        while (i > 0 && g_count) {
            if (buf[i - 1] == '\n' && ++lines == g_count) { start = i; break; }
            i--;
        }
        if (!g_count) start = len;
    }
    out(buf + start, len - start);
    free(buf);
    return n < 0 ? -1 : 0;
}

static int tail_fd(int fd)
{
    if (g_from_start) return skip_start(fd);
    struct stat st;
    if (fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && lseek(fd, 0, SEEK_CUR) >= 0)
        return tail_seekable(fd, st.st_size);
    return tail_stream(fd);
}

int main(int argc, char **argv)
{
    if (cervus_check_help_version(argc, argv, USAGE, "tail")) return 0;
    if (argc > 1 && (argv[1][0] == '-' || argv[1][0] == '+') && argv[1][1] >= '0' && argv[1][1] <= '9') {
        char *a = argv[1];
        char *e = a + 1;
        while (*e >= '0' && *e <= '9') e++;
        size_t len = (size_t)(e - a);
        char num[32];
        if (len < sizeof num && (!*e || !strcmp(e, "c") || !strcmp(e, "l") || !strcmp(e, "f") ||
                                 !strcmp(e, "cf") || !strcmp(e, "lf"))) {
            memcpy(num, a, len);
            num[len] = 0;
            if (strchr(e, 'c')) g_bytes = 1;
            parse_count(num);
            if (strchr(e, 'f')) {
                static char ff[] = "-f";
                argv[1] = ff;
            } else {
                for (int j = 1; j + 1 < argc; j++) argv[j] = argv[j + 1];
                argc--;
            }
        }
    }

    static const struct option longopts[] = {
        { "bytes",          required_argument, 0, 'c' },
        { "follow",         no_argument,       0, 'f' },
        { "lines",          required_argument, 0, 'n' },
        { "quiet",          no_argument,       0, 'q' },
        { "silent",         no_argument,       0, 'q' },
        { "sleep-interval", required_argument, 0, 's' },
        { "verbose",        no_argument,       0, 'v' },
        { 0, 0, 0, 0 }
    };
    int follow = 0, headers = -1, opt;
    while ((opt = getopt_long(argc, argv, "c:fn:qs:v", longopts, NULL)) != -1) {
        switch (opt) {
        case 'c': g_bytes = 1; if (parse_count(optarg) < 0) { fprintf(stderr, "tail: invalid number of bytes: '%s'\n", optarg); return 1; } break;
        case 'n': g_bytes = 0; if (parse_count(optarg) < 0) { fprintf(stderr, "tail: invalid number of lines: '%s'\n", optarg); return 1; } break;
        case 'f': follow = 1; break;
        case 'q': headers = 0; break;
        case 'v': headers = 1; break;
        case 's': g_sleep = atof(optarg); break;
        default: fputs(USAGE, stderr); return 1;
        }
    }

    static char *dash[] = { "-", NULL };
    char **files = optind < argc ? argv + optind : dash;
    int nf = optind < argc ? argc - optind : 1;
    if (headers < 0) headers = nf > 1;

    int *fds = calloc((size_t)nf, sizeof(int));
    int rc = 0;
    for (int i = 0; i < nf; i++) {
        int fd = strcmp(files[i], "-") ? open(files[i], O_RDONLY) : 0;
        fds[i] = fd;
        if (fd < 0) {
            fprintf(stderr, "tail: cannot open '%s' for reading: %s\n", files[i], strerror(errno));
            rc = 1;
            continue;
        }
        if (headers) printf("%s==> %s <==\n", i ? "\n" : "", strcmp(files[i], "-") ? files[i] : "standard input");
        if (tail_fd(fd) < 0) {
            fprintf(stderr, "tail: error reading '%s': %s\n", files[i], strerror(errno));
            rc = 1;
        }
        fflush(stdout);
    }

    if (follow) {
        int last = nf - 1;
        for (;;) {
            struct timespec ts = { (time_t)g_sleep, (long)((g_sleep - (time_t)g_sleep) * 1e9) };
            nanosleep(&ts, NULL);
            for (int i = 0; i < nf; i++) {
                if (fds[i] < 0) continue;
                struct stat st;
                if (fstat(fds[i], &st) == 0 && S_ISREG(st.st_mode)) {
                    off_t pos = lseek(fds[i], 0, SEEK_CUR);
                    if (st.st_size < pos) lseek(fds[i], 0, SEEK_SET);
                    if (st.st_size == pos) continue;
                }
                char buf[65536];
                ssize_t n = read(fds[i], buf, sizeof buf);
                if (n <= 0) continue;
                if (headers && i != last) { printf("\n==> %s <==\n", files[i]); last = i; }
                do out(buf, (size_t)n); while ((n = read(fds[i], buf, sizeof buf)) > 0);
                fflush(stdout);
            }
        }
    }
    for (int i = 0; i < nf; i++) if (fds[i] > 0) close(fds[i]);
    free(fds);
    return rc;
}
