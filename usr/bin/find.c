#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <cervus_util.h>

static int str_match(const char *name, const char *pat)
{
    while (*pat) {
        if (*pat == '*') {
            pat++;
            if (!*pat) return 1;
            while (*name) { if (str_match(name, pat)) return 1; name++; }
            return 0;
        } else if (*pat == '?') {
            if (!*name) return 0;
            name++; pat++;
        } else {
            if (*name != *pat) return 0;
            name++; pat++;
        }
    }
    return *name == '\0';
}

#define MAX_DEPTH 16

static void do_find(const char *dir, const char *pat, int depth)
{
    if (depth > MAX_DEPTH) return;
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.' &&
            (de->d_name[1] == '\0' ||
             (de->d_name[1] == '.' && de->d_name[2] == '\0'))) continue;
        char path[512];
        path_join(dir, de->d_name, path, sizeof(path));
        if (!pat || str_match(de->d_name, pat)) {
            fputs(path, stdout);
            if (de->d_type == DT_DIR) putchar('/');
            putchar('\n');
        }
        if (de->d_type == DT_DIR) do_find(path, pat, depth + 1);
    }
    closedir(d);
}

static int is_valid_path_chars(const char *s)
{
    for (; *s; s++) if ((unsigned char)*s < 0x20) return 0;
    return 1;
}

int main(int argc, char **argv)
{
    const char *cwd = get_cwd_flag(argc, argv);
    if (!cwd || !cwd[0]) cwd = "/";

    const char *dir = NULL;
    const char *pat = NULL;

    for (int i = 1; i < argc; i++) {
        if (is_shell_flag(argv[i])) continue;
        if (strcmp(argv[i], "-name") == 0) {
            if (i + 1 >= argc) {
                fputs("find: option '-name' requires an argument\n", stderr);
                return 1;
            }
            pat = argv[++i];
            if (!is_valid_path_chars(pat)) {
                fprintf(stderr, "find: invalid pattern '%s'\n", pat);
                return 1;
            }
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "find: unknown option: %s\n", argv[i]);
            fputs("usage: find [directory] [-name pattern]\n", stderr);
            return 1;
        } else {
            if (dir) {
                fputs("find: too many path arguments\n", stderr);
                return 1;
            }
            if (!is_valid_path_chars(argv[i])) {
                fprintf(stderr, "find: invalid path '%s'\n", argv[i]);
                return 1;
            }
            static char dir_buf[512];
            resolve_path(cwd, argv[i], dir_buf, sizeof(dir_buf));
            dir = dir_buf;
        }
    }
    if (!dir) dir = cwd;

    struct stat st;
    if (stat(dir, &st) < 0) {
        fprintf(stderr, "find: '%s': no such file or directory\n", dir);
        return 1;
    }
    if (st.st_type != DT_DIR) {
        fprintf(stderr, "find: '%s': not a directory\n", dir);
        return 1;
    }

    puts(dir);
    do_find(dir, pat, 0);
    return 0;
}
