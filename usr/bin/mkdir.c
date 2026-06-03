#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <cervus_util.h>

static int mkdir_p(const char *path, mode_t mode)
{
    char tmp[512];
    size_t len = strlen(path);
    if (len == 0 || len >= sizeof(tmp)) return -1;
    for (size_t i = 0; i <= len; i++) tmp[i] = path[i];
    for (size_t i = 1; i <= len; i++) {
        if (tmp[i] == '/' || tmp[i] == '\0') {
            char saved = tmp[i];
            tmp[i] = '\0';
            struct stat st;
            if (stat(tmp, &st) != 0) {
                int r = mkdir(tmp, mode);
                if (r < 0 && errno != EEXIST) { tmp[i] = saved; return r; }
            }
            tmp[i] = saved;
        }
    }
    return 0;
}

static const char USAGE[] =
    "Usage: mkdir [-p] [-m mode] dir ...\nCreate DIRECTORY(ies) if they do not exist.\n\n  -m M   set file mode (as in chmod, octal)\n  -p     no error if existing, make parent dirs as needed\n  -v     verbose (no-op currently)\n";

static void usage(void) { fputs(USAGE, stderr); }

int main(int argc, char **argv)
{
    if (cervus_check_help_version(argc, argv, USAGE, "mkdir")) return 0;

    int flag_p = 0;
    mode_t mode = 0755;

    int opt;
    while ((opt = getopt(argc, argv, "pm:v")) != -1) {
        switch (opt) {
            case 'p': flag_p = 1; break;
            case 'm': mode = (mode_t)strtol(optarg, NULL, 8); break;
            case 'v': break;
            default: usage(); return 1;
        }
    }
    if (optind >= argc) { usage(); return 1; }

    int rc = 0;
    for (int i = optind; i < argc; i++) {
        char path[512];
        snprintf(path, sizeof(path), "%s", argv[i]);
        int r = flag_p ? mkdir_p(path, mode) : mkdir(path, mode);
        if (r < 0) {
            if (errno == EEXIST && flag_p) continue;
            if (errno == EEXIST) {
                fprintf(stderr, "mkdir: '%s' already exists\n", argv[i]);
            } else {
                fprintf(stderr, "mkdir: cannot create '%s'\n", argv[i]);
            }
            rc = 1;
        }
    }
    return rc;
}
