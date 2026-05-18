#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <cervus_util.h>

static const char USAGE[] =
    "Usage: touch [-acm] file ...\nChange file timestamps or create empty files.\n\n  -a   change only access time (no-op, no atime)\n  -c   do not create files\n  -m   change only modification time (no-op)\n";

static void usage(void) { fputs(USAGE, stderr); }

int main(int argc, char **argv)
{
    if (cervus_check_help_version(argc, argv, USAGE, "touch")) return 0;
    const char *cwd = get_cwd_flag(argc, argv);
    argc = cervus_filter_args(argc, argv);

    int no_create = 0;
    int opt;
    while ((opt = getopt(argc, argv, "acm")) != -1) {
        switch (opt) {
            case 'c': no_create = 1; break;
            case 'a': break;
            case 'm': break;
            default: usage(); return 1;
        }
    }
    if (optind >= argc) { usage(); return 1; }

    int rc = 0;
    for (int i = optind; i < argc; i++) {
        char path[512];
        resolve_path(cwd, argv[i], path, sizeof(path));
        struct stat st;
        if (stat(path, &st) == 0) {
            if (st.st_type == DT_DIR) {
                fprintf(stderr, "touch: cannot touch '%s': is a directory\n", argv[i]);
                rc = 1;
            }
            continue;
        }
        if (no_create) continue;
        int fd = open(path, O_WRONLY | O_CREAT, 0644);
        if (fd < 0) { fprintf(stderr, "touch: cannot create '%s'\n", argv[i]); rc = 1; continue; }
        close(fd);
    }
    return rc;
}
