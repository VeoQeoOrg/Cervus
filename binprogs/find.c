#include "../apps/cervus_user.h"

static int str_match(const char *name, const char *pat) {
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

static void do_find(const char *dir, const char *pat, int depth) {
    if (depth > MAX_DEPTH) return;
    int fd = open(dir, O_RDONLY | O_DIRECTORY, 0);
    if (fd < 0) return;
    cervus_dirent_t de;
    while (readdir(fd, &de) == 0) {
        if (de.d_name[0] == '.' &&
            (de.d_name[1] == '\0' || (de.d_name[1] == '.' && de.d_name[2] == '\0')))
            continue;
        char path[512];
        path_join(dir, de.d_name, path, sizeof(path));
        if (!pat || str_match(de.d_name, pat)) {
            ws(path);
            if (de.d_type == DT_DIR) wc('/');
            wn();
        }
        if (de.d_type == DT_DIR) do_find(path, pat, depth + 1);
    }
    close(fd);
}

static int is_valid_path_chars(const char *s) {
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c < 0x20) return 0;
    }
    return 1;
}

CERVUS_MAIN(find_main) {
    const char *cwd = get_cwd_flag(argc, argv);
    if (!cwd || cwd[0] == '\0') cwd = "/";

    const char *dir = NULL;
    const char *pat = NULL;
    int bad_args = 0;

    for (int i = 1; i < argc; i++) {
        if (is_shell_flag(argv[i])) continue;

        if (strcmp(argv[i], "-name") == 0) {
            if (i + 1 >= argc) {
                fprintf(2, "find: option '-name' requires an argument\n");
                exit(1);
            }
            pat = argv[++i];
            if (!is_valid_path_chars(pat)) {
                fprintf(2, "find: invalid pattern '%s'\n", pat);
                exit(1);
            }
        } else if (argv[i][0] == '-') {
            fprintf(2, "find: unknown option: %s\n", argv[i]);
            fprintf(2, "usage: find [directory] [-name pattern]\n");
            exit(1);
        } else {
            if (dir != NULL) {
                fprintf(2, "find: too many path arguments\n");
                fprintf(2, "usage: find [directory] [-name pattern]\n");
                exit(1);
            }
            if (!is_valid_path_chars(argv[i])) {
                fprintf(2, "find: invalid path '%s'\n", argv[i]);
                exit(1);
            }
            static char dir_buf[512];
            resolve_path(cwd, argv[i], dir_buf, sizeof(dir_buf));
            dir = dir_buf;
        }
    }
    (void)bad_args;

    if (!dir) dir = cwd;

    cervus_stat_t st;
    if (stat(dir, &st) < 0) {
        fprintf(2, "find: '%s': no such file or directory\n", dir);
        exit(1);
    }
    if (st.st_type != DT_DIR) {
        fprintf(2, "find: '%s': not a directory\n", dir);
        exit(1);
    }

    ws(dir); wn();
    do_find(dir, pat, 0);
    exit(0);
}