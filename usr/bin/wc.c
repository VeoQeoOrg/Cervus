#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <cervus_util.h>

typedef struct {
    unsigned long lines;
    unsigned long words;
    unsigned long bytes;
    unsigned long chars;
} wc_count_t;

static void count_fd(int fd, wc_count_t *c)
{
    memset(c, 0, sizeof(*c));
    int in_word = 0;
    unsigned char buf[4096];
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        c->bytes += (unsigned long)n;
        for (ssize_t j = 0; j < n; j++) {
            unsigned char b = buf[j];
            if ((b & 0xC0) != 0x80) c->chars++;
            if (b == '\n') c->lines++;
            if (isspace(b)) in_word = 0;
            else if (!in_word) { c->words++; in_word = 1; }
        }
    }
}

static void emit(const wc_count_t *c, int sl, int sw, int sc, int sm,
                 const char *label, int width)
{
    int first = 1;
    if (sl) { printf("%*lu", first ? width : width + 1, c->lines); first = 0; }
    if (sw) { printf("%*lu", first ? width : width + 1, c->words); first = 0; }
    if (sm) { printf("%*lu", first ? width : width + 1, c->chars); first = 0; }
    if (sc) { printf("%*lu", first ? width : width + 1, c->bytes); first = 0; }
    if (label) printf(" %s", label);
    putchar('\n');
}

static const char USAGE[] =
    "Usage: wc [-clmw] [file ...]\n"
    "Print newline, word, and byte counts for each FILE.\n"
    "With no FILE, or - as FILE, read standard input.\n"
    "  -c   count bytes\n"
    "  -l   count newlines\n"
    "  -m   count characters\n"
    "  -w   count words\n";

static void usage(void) { fputs(USAGE, stderr); }

int main(int argc, char **argv)
{
    if (cervus_check_help_version(argc, argv, USAGE, "wc")) return 0;
    const char *cwd = get_cwd_flag(argc, argv);
    argc = cervus_filter_args(argc, argv);

    int sl = 0, sw = 0, sc = 0, sm = 0;
    int opt;
    while ((opt = getopt(argc, argv, "clmw")) != -1) {
        switch (opt) {
            case 'c': sc = 1; break;
            case 'l': sl = 1; break;
            case 'm': sm = 1; break;
            case 'w': sw = 1; break;
            default:  usage(); return 1;
        }
    }
    if (!sl && !sw && !sc && !sm) { sl = sw = sc = 1; }
    if (sm && sc) sc = 0;

    int file_count = argc - optind;

    int width = file_count > 1 ? 7 : 1;
    wc_count_t total;
    memset(&total, 0, sizeof(total));
    int rc = 0;

    if (file_count == 0) {
        wc_count_t c;
        count_fd(0, &c);
        emit(&c, sl, sw, sc, sm, NULL, width);
        return 0;
    }

    for (int i = optind; i < argc; i++) {
        char resolved[512];
        resolve_path(cwd, argv[i], resolved, sizeof(resolved));
        int fd = open(resolved, O_RDONLY);
        if (fd < 0) {
            fprintf(stderr, "wc: cannot open '%s'\n", argv[i]);
            rc = 1;
            continue;
        }
        wc_count_t c;
        count_fd(fd, &c);
        close(fd);
        emit(&c, sl, sw, sc, sm, argv[i], width);
        total.lines += c.lines;
        total.words += c.words;
        total.bytes += c.bytes;
        total.chars += c.chars;
    }
    if (file_count > 1) emit(&total, sl, sw, sc, sm, "total", width);
    return rc;
}
