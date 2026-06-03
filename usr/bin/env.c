#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <cervus_util.h>

extern char **environ;

static const char USAGE[] =
    "Usage: env [-i] [name=value ...] [name]\nPrint the environment, or look up NAME.\n\n  -i   start with empty environment (do not inherit)\n";

static void usage(void) { fputs(USAGE, stderr); }

int main(int argc, char **argv)
{
    if (cervus_check_help_version(argc, argv, USAGE, "env")) return 0;
    int keep = 1;

    int opt;
    while ((opt = getopt(argc, argv, "i")) != -1) {
        switch (opt) {
            case 'i': keep = 0; break;
            default: usage(); return 1;
        }
    }

    while (optind < argc && strchr(argv[optind], '=')) {
        putenv(argv[optind]);
        optind++;
    }

    if (optind < argc) {
        const char *name = argv[optind];
        const char *val = getenv(name);
        if (val) { puts(val); return 0; }
        fprintf(stderr, "env: variable not set: %s\n", name);
        return 1;
    }

    if (keep && environ) {
        int found = 0;
        for (char **e = environ; *e; e++) { puts(*e); found++; }
        if (!found)
            fputs(C_GRAY "(no environment variables set)" C_RESET "\n", stdout);
    }
    return 0;
}
