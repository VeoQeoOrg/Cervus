#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <cervus_util.h>

static int mkdir_p(const char *path)
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
                int r = mkdir(tmp, 0755);
                if (r < 0 && errno != EEXIST) { tmp[i] = saved; return r; }
            }
            tmp[i] = saved;
        }
    }
    return 0;
}

int main(int argc, char **argv)
{
    const char *cwd_str = get_cwd_flag(argc, argv);
    int found = 0;
    int flag_p = 0;

    for (int i = 1; i < argc; i++) {
        if (is_shell_flag(argv[i])) continue;
        if (strcmp(argv[i], "-p") == 0) { flag_p = 1; continue; }
        if (argv[i][0] == '-') continue;
        found++;

        char path[512];
        resolve_path(cwd_str, argv[i], path, sizeof(path));

        int r = flag_p ? mkdir_p(path) : mkdir(path, 0755);
        if (r < 0) {
            if (errno == EEXIST) {
                if (!flag_p)
                    fprintf(stderr, "mkdir: '%s' already exists\n", argv[i]);
            } else {
                fprintf(stderr, "mkdir: cannot create '%s' (errno %d)\n",
                        argv[i], errno);
            }
        }
    }
    if (!found) {
        fputs("Usage: mkdir [-p] <dir> [dir2 ...]\n", stdout);
        return 1;
    }
    return 0;
}
