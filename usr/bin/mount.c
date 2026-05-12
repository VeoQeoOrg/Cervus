#include <stdio.h>
#include <sys/cervus.h>
#include <cervus_util.h>

int main(int argc, char **argv)
{
    int real_argc = 0;
    for (int i = 0; i < argc; i++)
        if (!is_shell_flag(argv[i])) real_argc++;

    if (real_argc < 3) {
        fputs("Usage: mount <device> <mountpoint>\n"
              "  e.g: mount hda /mnt/disk\n"
              "\nMounts /dev/<device> with Ext2 at <mountpoint>.\n",
              stdout);
        return 1;
    }

    const char *devname = NULL, *path = NULL;
    int ai = 0;
    for (int i = 0; i < argc; i++) {
        if (is_shell_flag(argv[i])) continue;
        if (ai == 1) devname = argv[i];
        if (ai == 2) path    = argv[i];
        ai++;
    }
    if (!devname || !path) {
        fputs("mount: missing arguments\n", stderr);
        return 1;
    }

    int r = cervus_disk_mount(devname, path);
    if (r < 0) {
        fprintf(stderr, "mount: failed to mount %s at %s\n", devname, path);
        return 1;
    }
    printf("Mounted %s at %s\n", devname, path);
    return 0;
}
