#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include <crypto.h>
#include <cervus_util.h>

static const char USAGE[] =
    "Usage: sha256sum [OPTION]... [FILE]...\n"
    "Print or check SHA256 (256-bit) checksums.\n\n"
    "With no FILE, or when FILE is -, read standard input.\n\n"
    "  -b, --binary   read in binary mode (marks the name with '*')\n"
    "  -c, --check    read checksums from the FILEs and check them\n"
    "  -t, --text     read in text mode (default)\n"
    "      --quiet    don't print OK for each successfully verified file\n"
    "      --status   don't output anything, status code shows success\n"
    "  -w, --warn     warn about improperly formatted checksum lines\n";

static int g_quiet, g_status, g_warn;

static int hash_file(const char *name, uint8_t out[32])
{
    FILE *f = strcmp(name, "-") ? fopen(name, "rb") : stdin;
    if (!f) return -1;
    sha256_ctx c;
    sha256_init(&c);
    unsigned char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) sha256_update(&c, buf, n);
    int err = ferror(f);
    if (f != stdin) fclose(f);
    if (err) return -1;
    sha256_final(&c, out);
    return 0;
}

static void hex(const uint8_t d[32], char out[65])
{
    static const char digits[] = "0123456789abcdef";
    for (int i = 0; i < 32; i++) {
        out[2 * i] = digits[d[i] >> 4];
        out[2 * i + 1] = digits[d[i] & 15];
    }
    out[64] = 0;
}

static int check_list(const char *list)
{
    FILE *f = strcmp(list, "-") ? fopen(list, "r") : stdin;
    if (!f) { fprintf(stderr, "sha256sum: %s: %s\n", list, strerror(errno)); return 1; }
    char *line = NULL;
    size_t cap = 0;
    ssize_t len;
    int bad_fmt = 0, failed = 0, unreadable = 0, ok = 0, lineno = 0;
    while ((len = getline(&line, &cap, f)) > 0) {
        lineno++;
        while (len && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = 0;
        if (!len || line[0] == '#') continue;
        int good = len > 66 && line[64] == ' ' && (line[65] == ' ' || line[65] == '*');
        for (int i = 0; good && i < 64; i++)
            if (!strchr("0123456789abcdefABCDEF", line[i])) good = 0;
        if (!good) {
            bad_fmt++;
            if (g_warn) fprintf(stderr, "sha256sum: %s: %d: improperly formatted SHA256 checksum line\n", list, lineno);
            continue;
        }
        const char *name = line + 66;
        uint8_t d[32];
        char h[65];
        if (hash_file(name, d) < 0) {
            unreadable++;
            if (!g_status) printf("%s: FAILED open or read\n", name);
            continue;
        }
        hex(d, h);
        if (strncasecmp(h, line, 64) == 0) {
            ok++;
            if (!g_status && !g_quiet) printf("%s: OK\n", name);
        } else {
            failed++;
            if (!g_status) printf("%s: FAILED\n", name);
        }
    }
    free(line);
    if (f != stdin) fclose(f);
    if (!g_status) {
        if (bad_fmt) fprintf(stderr, "sha256sum: WARNING: %d line%s improperly formatted\n", bad_fmt, bad_fmt == 1 ? " is" : "s are");
        if (unreadable) fprintf(stderr, "sha256sum: WARNING: %d listed file%s could not be read\n", unreadable, unreadable == 1 ? "" : "s");
        if (failed) fprintf(stderr, "sha256sum: WARNING: %d computed checksum%s did NOT match\n", failed, failed == 1 ? "" : "s");
    }
    if (!ok && !failed && !unreadable) {
        if (!g_status) fprintf(stderr, "sha256sum: %s: no properly formatted SHA256 checksum lines found\n", list);
        return 1;
    }
    return failed || unreadable;
}

int main(int argc, char **argv)
{
    if (cervus_check_help_version(argc, argv, USAGE, "sha256sum")) return 0;
    static const struct option longopts[] = {
        { "binary", no_argument, 0, 'b' },
        { "check",  no_argument, 0, 'c' },
        { "text",   no_argument, 0, 't' },
        { "warn",   no_argument, 0, 'w' },
        { "quiet",  no_argument, 0, 'q' },
        { "status", no_argument, 0, 's' },
        { 0, 0, 0, 0 }
    };
    int check = 0, binary = 0, opt;
    while ((opt = getopt_long(argc, argv, "bctw", longopts, NULL)) != -1) {
        switch (opt) {
        case 'b': binary = 1; break;
        case 't': binary = 0; break;
        case 'c': check = 1; break;
        case 'w': g_warn = 1; break;
        case 'q': g_quiet = 1; break;
        case 's': g_status = 1; break;
        default: fputs(USAGE, stderr); return 1;
        }
    }
    static char *dash[] = { "-", NULL };
    char **files = optind < argc ? argv + optind : dash;
    int n = optind < argc ? argc - optind : 1;
    int rc = 0;
    for (int i = 0; i < n; i++) {
        if (check) { rc |= check_list(files[i]); continue; }
        uint8_t d[32];
        char h[65];
        if (hash_file(files[i], d) < 0) {
            fprintf(stderr, "sha256sum: %s: %s\n", files[i], strerror(errno));
            rc = 1;
            continue;
        }
        hex(d, h);
        printf("%s %c%s\n", h, binary ? '*' : ' ', files[i]);
    }
    return rc;
}
