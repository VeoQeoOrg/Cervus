#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <cervus_util.h>

static const char USAGE[] =
    "Usage: ln -s target link_name\nCreate a symbolic link to TARGET with the name LINK_NAME.\n\n  -s   make symbolic link (required: hard links are not supported)\n  -f   remove existing destination\n";

int main(int argc, char **argv)
{
    if (cervus_check_help_version(argc, argv, USAGE, "ln")) return 0;

    int symbolic = 0, force = 0;
    int opt;
    while ((opt = getopt(argc, argv, "sf")) != -1) {
        if (opt == 's') symbolic = 1;
        else if (opt == 'f') force = 1;
        else { fputs(USAGE, stderr); return 1; }
    }
    if (argc - optind != 2) { fputs(USAGE, stderr); return 1; }

    if (!symbolic) {
        fputs("ln: hard links are not supported, use ln -s\n", stderr);
        return 1;
    }

    const char *target = argv[optind];
    const char *link_  = argv[optind + 1];

    if (force) unlink(link_);

    if (symlink(target, link_) < 0) {
        fprintf(stderr, "ln: cannot create symlink '%s' -> '%s'\n", link_, target);
        return 1;
    }
    return 0;
}
