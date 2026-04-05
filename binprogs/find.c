#include "../apps/cervus_user.h"

static int str_match(const char *name, const char *pat) {
    while (*pat) {
        if (*pat == '*') {
            pat++;
            if (!*pat)
                return 1;
            while (*name) {
                if (str_match(name, pat))
                    return 1;
                name++;
            }
            return 0;
        } else if (*pat == '?') {
            if (!*name)
                return 0;
            name++;
            pat++;
        } else {
            if (*name != *pat)
                return 0;
            name++;
            pat++;
        }
    }
    return *name == '\0';
}

#define MAX_DEPTH 16

static void do_find(const char *dir, const char *pat, int depth) {
    if (depth > MAX_DEPTH)
        return;
    int fd = open(dir, O_RDONLY | O_DIRECTORY, 0);
    if (fd < 0)
        return;
    cervus_dirent_t de;
    while (readdir(fd, &de) == 0) {
        if (de.d_name[0] == '.' &&
            (de.d_name[1] == '\0' || (de.d_name[1] == '.' && de.d_name[2] == '\0')))
            continue;
        char path[512];
        path_join(dir, de.d_name, path, sizeof(path));
        if (!pat || str_match(de.d_name, pat)) {
            ws(path);
            if (de.d_type == 1)
                write(1, "/", 1);
            wn();
        }
        if (de.d_type == 1)
            do_find(path, pat, depth + 1);
    }
    close(fd);
}

CERVUS_MAIN(find_main) {
    const char *cwd = get_cwd_flag(argc, argv);
    const char *dir = NULL;
    const char *pat = NULL;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] == '-')
            continue;
        if (argv[i][0] == '-') {
            if (argv[i][1] == 'n' && argv[i][2] == 'a' && argv[i][3] == 'm' && argv[i][4] == 'e') {
                if (i + 1 < argc)
                    pat = argv[++i];
            }
        } else {
            dir = argv[i];
        }
    }
    if (!dir)
        dir = cwd;
    ws(dir);
    wn();
    do_find(dir, pat, 0);
    exit(0);
}