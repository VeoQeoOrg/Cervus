#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <cervus_util.h>

extern char **environ;

static const char USAGE[] =
    "Usage: env [-i] [-u name] [name=value ...] [command [argument ...]]\n"
    "Run COMMAND in a changed environment, or print the environment.\n\n"
    "  -i        start with an empty environment\n"
    "  -u name   remove NAME from the environment\n";

static char *g_empty[1];

int main(int argc, char **argv)
{
    for (int i = 1; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        if (!strcmp(argv[i], "--")) break;
        if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-?")) { fputs(USAGE, stdout); return 0; }
        if (!strcmp(argv[i], "--version")) { puts("env (" CERVUS_VERSION_STR ")"); return 0; }
    }

    int opt;
    while ((opt = getopt(argc, argv, "iu:")) != -1) {
        switch (opt) {
            case 'i': environ = g_empty; break;
            case 'u': unsetenv(optarg); break;
            default: fputs(USAGE, stderr); return 125;
        }
    }
    if (optind < argc && !strcmp(argv[optind], "-")) {
        environ = g_empty;
        optind++;
    }

    while (optind < argc && strchr(argv[optind], '=')) {
        if (putenv(argv[optind]) != 0) {
            fprintf(stderr, "env: cannot set %s: %s\n", argv[optind], strerror(errno));
            return 125;
        }
        optind++;
    }

    if (optind < argc) {
        execvp(argv[optind], argv + optind);
        int err = errno;
        fprintf(stderr, "env: %s: %s\n", argv[optind], strerror(err));
        return err == ENOENT ? 127 : 126;
    }

    if (environ)
        for (char **e = environ; *e; e++) puts(*e);
    return 0;
}
