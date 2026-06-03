#include <stdio.h>
#include <sys/cervus.h>
#include <cervus_util.h>


static const char USAGE[] =
    "Usage: umount path\nUnmount filesystem at PATH.\n";
int main(int argc, char **argv)
{
    if (cervus_check_help_version(argc, argv, USAGE, "umount")) return 0;
    const char *path = NULL;
    int ai = 0;
    for (int i = 0; i < argc; i++) {
        if (ai == 1) { path = argv[i]; break; }
        ai++;
    }
    if (!path) {
        fputs("Usage: umount <mountpoint>\n", stdout);
        return 1;
    }
    if (path[0] == '/' && path[1] == '\0') {
        if (!cervus_confirm("unmount", "/",
                "the root filesystem holds every running program and open file")) {
            fputs("umount: aborted\n", stderr);
            return 1;
        }
    }
    int r = cervus_disk_umount(path);
    if (r < 0) {
        fputs("umount: failed\n", stderr);
        return 1;
    }
    printf("Unmounted %s\n", path);
    return 0;
}
