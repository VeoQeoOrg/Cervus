#include <stdio.h>
#include <sys/cervus.h>
#include <cervus_util.h>

int main(int argc, char **argv)
{
    const char *path = NULL;
    int ai = 0;
    for (int i = 0; i < argc; i++) {
        if (is_shell_flag(argv[i])) continue;
        if (ai == 1) { path = argv[i]; break; }
        ai++;
    }
    if (!path) {
        fputs("Usage: umount <mountpoint>\n", stdout);
        return 1;
    }
    int r = cervus_disk_umount(path);
    if (r < 0) {
        fputs("umount: failed\n", stderr);
        return 1;
    }
    printf("Unmounted %s\n", path);
    return 0;
}
