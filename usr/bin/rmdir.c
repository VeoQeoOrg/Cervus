#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <cervus_util.h>

static const char USAGE[] =
    "Usage: rmdir [-p] dir ...\nRemove empty DIRECTORY(ies).\n\n  -p   remove DIRECTORY and its ancestors\n";

int main(int argc, char **argv)
{
    if (cervus_check_help_version(argc, argv, USAGE, "rmdir")) return 0;

    int flag_p = 0;
    int opt;
    while ((opt = getopt(argc, argv, "p")) != -1) {
        if (opt == 'p') flag_p = 1;
        else { fputs(USAGE, stderr); return 1; }
    }
    if (optind >= argc) { fputs(USAGE, stderr); return 1; }

    int rc = 0;
    for (int i = optind; i < argc; i++) {
        if (rmdir(argv[i]) < 0) {
            fprintf(stderr, "rmdir: failed to remove '%s'\n", argv[i]);
            rc = 1;
            continue;
        }
        if (flag_p) {
            char path[512];
            snprintf(path, sizeof(path), "%s", argv[i]);
            char *slash;
            while ((slash = strrchr(path, '/')) != NULL && slash != path) {
                *slash = '\0';
                if (rmdir(path) < 0) break;
            }
        }
    }
    return rc;
}
