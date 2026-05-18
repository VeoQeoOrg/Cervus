#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <cervus_util.h>

static const char USAGE[] =
    "Usage: env [-i] [name=value ...] [name]\nPrint the environment, or look up NAME.\n\n  -i   start with empty environment (do not inherit)\n";

static void usage(void) { fputs(USAGE, stderr); }

int main(int argc, char **argv)
{
    if (cervus_check_help_version(argc, argv, USAGE, "env")) return 0;
    int keep = 1;

    argc = cervus_filter_args(argc, argv);

    int opt;
    while ((opt = getopt(argc, argv, "i")) != -1) {
        switch (opt) {
            case 'i': keep = 0; break;
            default: usage(); return 1;
        }
    }

    if (optind < argc && !strchr(argv[optind], '=')) {
        const char *name = argv[optind];
        const char *val = getenv_argv(argc, argv, name, NULL);
        if (val) { puts(val); return 0; }
        fprintf(stderr, "env: variable not set: %s\n", name);
        return 1;
    }

    if (keep) {
        int found = 0;
        for (int i = 1; i < __cervus_argc; i++) {
            const char *a = __cervus_argv[i];
            if (a && a[0] == '-' && a[1] == '-' &&
                a[2] == 'e' && a[3] == 'n' && a[4] == 'v' && a[5] == ':') {
                puts(a + 6);
                found++;
            }
        }
        if (!found && optind >= argc)
            fputs(C_GRAY "(no environment variables set)" C_RESET "\n", stdout);
    }

    for (int i = optind; i < argc; i++) {
        char *eq = strchr(argv[i], '=');
        if (eq) puts(argv[i]);
    }
    return 0;
}
