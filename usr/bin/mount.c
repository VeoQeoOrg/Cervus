#include <stdio.h>
#include <sys/cervus.h>
#include <cervus_util.h>


static const char USAGE[] =
    "Usage: mount device path | mount\nMount filesystem; with no args, list mounts.\n";
int main(int argc, char **argv)
{
    if (cervus_check_help_version(argc, argv, USAGE, "mount")) return 0;
    if (argc < 3) {
        fputs("Usage: mount <device> <mountpoint>\n"
              "  e.g: mount hda /mnt/disk\n"
              "\nMounts /dev/<device> with Ext2 at <mountpoint>.\n",
              stdout);
        return 1;
    }

    const char *devname = argv[1];
    const char *path    = argv[2];
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
