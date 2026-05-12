#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <dirent.h>
#include <cervus_util.h>

int main(int argc, char **argv)
{
    const char *cwd_str = get_cwd_flag(argc, argv);
    int recursive = 0;
    int found = 0;

    for (int i = 1; i < argc; i++) {
        if (is_shell_flag(argv[i])) continue;
        if (strcmp(argv[i], "-r")  == 0 ||
            strcmp(argv[i], "-R")  == 0 ||
            strcmp(argv[i], "-rf") == 0) { recursive = 1; continue; }
        found++;
        char path[512];
        resolve_path(cwd_str, argv[i], path, sizeof(path));

        struct stat st;
        if (stat(path, &st) < 0) {
            fprintf(stderr, "rm: cannot remove '%s': No such file or directory\n",
                    argv[i]);
            continue;
        }

        int r;
        if (st.st_type == DT_DIR) {
            if (!recursive) {
                fprintf(stderr, "rm: '%s' is a directory (use -r)\n", argv[i]);
                continue;
            }
            r = rmdir(path);
        } else {
            r = unlink(path);
        }
        if (r < 0)
            fprintf(stderr, "rm: failed to remove '%s' (errno %d)\n",
                    argv[i], errno);
    }
    if (!found) {
        fputs("Usage: rm [-r] <file|dir> ...\n", stdout);
        return 1;
    }
    return 0;
}
