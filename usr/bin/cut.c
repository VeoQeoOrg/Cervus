#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <cervus_util.h>

static const char USAGE[] =
    "Usage: cut -f list [-d delim] [file...]\n       cut -c list [file...]\nPrint selected fields or characters from each line.\n\n  -f L   select fields (e.g. 1,3-5)\n  -c L   select characters\n  -d D   field delimiter (default TAB)\n";

#define MAXR 64
static int r_lo[MAXR], r_hi[MAXR];
static int nr;

static int parse_list(const char *s)
{
    nr = 0;
    while (*s && nr < MAXR) {
        int lo = 0, hi = 0;
        if (*s == '-') { lo = 1; }
        else { lo = atoi(s); while (*s >= '0' && *s <= '9') s++; }
        if (*s == '-') {
            s++;
            if (*s >= '0' && *s <= '9') { hi = atoi(s); while (*s >= '0' && *s <= '9') s++; }
            else hi = 1 << 30;
        } else hi = lo;
        if (lo <= 0 || hi < lo) return -1;
        r_lo[nr] = lo; r_hi[nr] = hi; nr++;
        if (*s == ',') s++;
        else if (*s) return -1;
    }
    return nr ? 0 : -1;
}

static int in_list(int idx)
{
    for (int i = 0; i < nr; i++)
        if (idx >= r_lo[i] && idx <= r_hi[i]) return 1;
    return 0;
}

static void do_line(char *line, int mode_c, char delim)
{
    if (mode_c) {
        int len = (int)strlen(line);
        for (int i = 0; i < len; i++)
            if (in_list(i + 1)) putchar(line[i]);
        putchar('\n');
        return;
    }
    if (!strchr(line, delim)) { puts(line); return; }
    int field = 1, first = 1;
    char *p = line;
    while (p) {
        char *next = strchr(p, delim);
        if (next) *next = '\0';
        if (in_list(field)) {
            if (!first) putchar(delim);
            fputs(p, stdout);
            first = 0;
        }
        field++;
        p = next ? next + 1 : NULL;
    }
    putchar('\n');
}

static void do_stream(FILE *f, int mode_c, char delim)
{
    char line[4096];
    while (fgets(line, sizeof(line), f)) {
        size_t l = strlen(line);
        if (l && line[l-1] == '\n') line[l-1] = '\0';
        do_line(line, mode_c, delim);
    }
}

int main(int argc, char **argv)
{
    if (cervus_check_help_version(argc, argv, USAGE, "cut")) return 0;

    int mode_c = -1;
    char delim = '\t';
    int opt;
    while ((opt = getopt(argc, argv, "f:c:d:")) != -1) {
        switch (opt) {
            case 'f': mode_c = 0; if (parse_list(optarg) < 0) { fputs(USAGE, stderr); return 1; } break;
            case 'c': mode_c = 1; if (parse_list(optarg) < 0) { fputs(USAGE, stderr); return 1; } break;
            case 'd': delim = optarg[0]; break;
            default: fputs(USAGE, stderr); return 1;
        }
    }
    if (mode_c < 0) { fputs(USAGE, stderr); return 1; }

    if (optind >= argc) {
        do_stream(stdin, mode_c, delim);
        return 0;
    }
    int rc = 0;
    for (int i = optind; i < argc; i++) {
        FILE *f = fopen(argv[i], "r");
        if (!f) { fprintf(stderr, "cut: cannot open '%s'\n", argv[i]); rc = 1; continue; }
        do_stream(f, mode_c, delim);
        fclose(f);
    }
    return rc;
}
