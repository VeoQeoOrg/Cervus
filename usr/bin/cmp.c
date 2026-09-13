#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <cervus_util.h>

static const char USAGE[] =
    "Usage: cmp [-l] [-s] file1 file2\n"
    "Report the first place two files differ, or say nothing if they match.\n"
    "\n"
    "  -l  list every differing byte as offset and the two values\n"
    "  -s  say nothing, report the answer in the exit status\n"
    "\n"
    "Exits 0 if the files are the same, 1 if they differ, 2 on an error.\n";

int main(int argc, char **argv)
{
    if (cervus_check_help_version(argc, argv, USAGE, "cmp")) return 0;
    argc = cervus_end_of_options(argc, argv);

    int list = 0, silent = 0, i = 1;
    for (; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        for (const char *p = argv[i] + 1; *p; p++) {
            if (*p == 'l') list = 1;
            else if (*p == 's') silent = 1;
            else { fputs(USAGE, stderr); return 2; }
        }
    }
    if (argc - i != 2) { fputs(USAGE, stderr); return 2; }

    const char *n1 = argv[i], *n2 = argv[i + 1];
    FILE *a = strcmp(n1, "-") ? fopen(n1, "rb") : stdin;
    if (!a) { if (!silent) fprintf(stderr, "cmp: cannot open %s\n", n1); return 2; }
    FILE *b = strcmp(n2, "-") ? fopen(n2, "rb") : stdin;
    if (!b) {
        if (!silent) fprintf(stderr, "cmp: cannot open %s\n", n2);
        if (a != stdin) fclose(a);
        return 2;
    }

    unsigned long long off = 0, line = 1;
    int rc = 0;
    for (;;) {
        int ca = fgetc(a);
        int cb = fgetc(b);

        if (ca == EOF && cb == EOF) break;

        if (ca == EOF || cb == EOF) {
            if (!silent)
                fprintf(stderr, "cmp: EOF on %s after byte %llu\n",
                        ca == EOF ? n1 : n2, off);
            rc = 1;
            break;
        }

        off++;
        if (ca != cb) {
            rc = 1;
            if (silent) break;
            if (list) {
                printf("%llu %o %o\n", off, (unsigned)ca, (unsigned)cb);
            } else {
                printf("%s %s differ: byte %llu, line %llu\n", n1, n2, off, line);
                break;
            }
        }
        if (ca == '\n') line++;
    }

    if (a != stdin) fclose(a);
    if (b != stdin) fclose(b);
    return rc;
}
