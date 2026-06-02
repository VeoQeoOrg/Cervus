#include <stdio.h>
#include <string.h>
#include <sys/cervus.h>
#include <cervus_util.h>

static const char USAGE[] =
    "Usage: eject <device>\n"
    "Eject the media in an ATAPI CD/DVD drive.\n"
    "Example:  eject /dev/sdb\n";

int main(int argc, char **argv)
{
    if (cervus_check_help_version(argc, argv, USAGE, "eject")) return 0;

    const char *devname = NULL;
    for (int i = 1; i < argc; i++) {
        if (is_shell_flag(argv[i])) continue;
        if (!devname) devname = argv[i];
    }
    if (!devname) { fputs(USAGE, stdout); return 1; }

    const char *short_name = devname;
    if (strncmp(short_name, "/dev/", 5) == 0) short_name += 5;

    int r = cervus_disk_eject(short_name);
    if (r < 0) {
        fprintf(stderr, "eject: %s: %d (only ATAPI CD/DVD drives support eject)\n",
                short_name, r);
        return 1;
    }
    printf("Ejected /dev/%s\n", short_name);
    return 0;
}
