#include <stdio.h>
#include <string.h>
#include <sys/cervus.h>
#include <cervus_util.h>

static const char USAGE[] =
    "Usage: mkfs [-t fs] device [label]\n"
    "  fs: ext2 (default), ext4, fat32 or udf\n"
    "Examples:\n"
    "  mkfs sda1                # ext2\n"
    "  mkfs -t ext4 sda1        # ext4 (extents)\n"
    "  mkfs -t fat32 sda1 ESP   # FAT32 with label ESP\n"
    "  mkfs -t udf sda1 DATA    # UDF, readable by Linux and Windows\n";

int main(int argc, char **argv)
{
    if (cervus_check_help_version(argc, argv, USAGE, "mkfs")) return 0;
    argc = cervus_end_of_options(argc, argv);

    const char *fs = "ext2";
    const char *devname = NULL;
    const char *label = NULL;
    int positional = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-t") == 0) {
            if (i + 1 >= argc) { fputs("mkfs: -t requires fs type\n", stderr); return 1; }
            fs = argv[++i];
            continue;
        }
        if (positional == 0)      devname = argv[i];
        else if (positional == 1) label   = argv[i];
        positional++;
    }
    if (!devname) { fputs(USAGE, stdout); return 1; }

    int is_fat32 = (strcmp(fs, "fat32") == 0 || strcmp(fs, "vfat") == 0);
    int is_ext4  = (strcmp(fs, "ext4")  == 0);
    int is_udf   = (strcmp(fs, "udf")   == 0);
    int is_ext2  = (strcmp(fs, "ext2")  == 0);
    if (!is_fat32 && !is_ext2 && !is_ext4 && !is_udf) {
        fprintf(stderr, "mkfs: unknown fs type '%s' (use ext2, ext4, fat32 or udf)\n", fs);
        return 1;
    }

    char target[128];
    snprintf(target, sizeof(target), "format /dev/%s as %s", devname, fs);
    if (!cervus_confirm(target, NULL,
            "every existing file on this device will be erased and unrecoverable")) {
        fputs("mkfs: aborted\n", stderr);
        return 1;
    }

    printf("Formatting %s as %s...\n", devname, fs);
    int r;
    if (is_udf)        r = cervus_disk_mkfs_udf  (devname, label ? label : devname);
    else if (is_fat32) r = cervus_disk_mkfs_fat32(devname, label ? label : devname);
    else               r = cervus_disk_format    (devname, label ? label : devname, is_ext4);
    if (r < 0) {
        fprintf(stderr, "mkfs: %s format failed (%d)\n", fs, r);
        return 1;
    }
    const char *made = is_udf ? "UDF" : is_fat32 ? "FAT32" : is_ext4 ? "ext4" : "ext2";
    printf("Done. %s created on %s\n", made, devname);
    return 0;
}
