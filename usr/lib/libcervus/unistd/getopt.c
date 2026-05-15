#include <unistd.h>
#include <stdio.h>
#include <stddef.h>

char *optarg = NULL;
int   optind = 1;
int   optopt = 0;
int   opterr = 1;

static int __opt_subidx = 1;

int getopt(int argc, char *const argv[], const char *optstring)
{
    if (!optstring) optstring = "";
    int colon_mode = (optstring[0] == ':');
    const char *opts = colon_mode ? optstring + 1 : optstring;

    optarg = NULL;

    if (optind >= argc) return -1;

    char *cur = argv[optind];
    if (!cur || cur[0] != '-' || cur[1] == '\0') return -1;
    if (cur[0] == '-' && cur[1] == '-' && cur[2] == '\0') {
        optind++;
        return -1;
    }

    char ch = cur[__opt_subidx];
    if (ch == '\0') {
        optind++;
        __opt_subidx = 1;
        return getopt(argc, argv, optstring);
    }

    const char *pp = opts;
    while (*pp && *pp != ch) pp++;
    if (*pp == '\0' || ch == ':') {
        optopt = ch;
        if (opterr && !colon_mode) {
            const char *prog = argv[0] ? argv[0] : "?";
            fprintf(stderr, "%s: invalid option -- '%c'\n", prog, ch);
        }
        __opt_subidx++;
        if (cur[__opt_subidx] == '\0') {
            optind++;
            __opt_subidx = 1;
        }
        return '?';
    }

    if (pp[1] == ':') {
        if (cur[__opt_subidx + 1] != '\0') {
            optarg = &cur[__opt_subidx + 1];
            optind++;
            __opt_subidx = 1;
            return ch;
        }
        if (optind + 1 >= argc) {
            optopt = ch;
            optind++;
            __opt_subidx = 1;
            if (opterr && !colon_mode) {
                const char *prog = argv[0] ? argv[0] : "?";
                fprintf(stderr, "%s: option requires an argument -- '%c'\n", prog, ch);
            }
            return colon_mode ? ':' : '?';
        }
        optarg = argv[optind + 1];
        optind += 2;
        __opt_subidx = 1;
        return ch;
    }

    __opt_subidx++;
    if (cur[__opt_subidx] == '\0') {
        optind++;
        __opt_subidx = 1;
    }
    return ch;
}
