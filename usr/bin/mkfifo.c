#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cervus_util.h>

static const char USAGE[] =
    "Usage: mkfifo [-m mode] file ...\n"
    "Create named pipes (FIFOs).\n\n"
    "  -m MODE  permissions, octal or symbolic as in chmod (default a=rw minus umask)\n";

int main(int argc, char **argv)
{
    if (cervus_check_help_version(argc, argv, USAGE, "mkfifo")) return 0;
    argc = cervus_end_of_options(argc, argv);

    void *set = NULL;
    int opt;
    while ((opt = getopt(argc, argv, "m:")) != -1) {
        if (opt != 'm') { fputs(USAGE, stderr); return 2; }
        free(set);
        set = setmode(optarg);
        if (!set) { fprintf(stderr, "mkfifo: invalid mode '%s'\n", optarg); return 2; }
    }
    if (optind >= argc) { fputs(USAGE, stderr); return 2; }

    int rc = 0;
    for (int i = optind; i < argc; i++) {
        mode_t mode = set ? getmode(set, 0666) : 0666;
        if (mkfifo(argv[i], mode) < 0) {
            fprintf(stderr, "mkfifo: cannot create '%s': %s\n", argv[i], strerror(errno));
            rc = 1;
            continue;
        }
        if (set && chmod(argv[i], mode) < 0) {
            fprintf(stderr, "mkfifo: cannot set mode of '%s': %s\n", argv[i], strerror(errno));
            rc = 1;
        }
    }
    free(set);
    return rc;
}
