#include <stdio.h>
#include <sys/cervus.h>
#include <cervus_util.h>

int main(int argc, char **argv)
{
    const char *devname = NULL, *label = NULL;
    int ai = 0;
    for (int i = 0; i < argc; i++) {
        if (is_shell_flag(argv[i])) continue;
        if (ai == 1) devname = argv[i];
        if (ai == 2) label   = argv[i];
        ai++;
    }
    if (!devname) {
        fputs("Usage: mkfs <device> [label]\n"
              "  e.g: mkfs hda mydisk\n"
              "\nFormats /dev/<device> with Ext2.\n"
              "WARNING: all data on device will be lost!\n",
              stdout);
        return 1;
    }
    printf("Formatting %s...\n", devname);
    int r = cervus_disk_format(devname, label ? label : devname);
    if (r < 0) {
        fputs("mkfs: format failed\n", stderr);
        return 1;
    }
    printf("Done. Ext2 created on %s\n", devname);
    return 0;
}
