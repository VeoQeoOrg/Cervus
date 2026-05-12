#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <cervus_util.h>

int main(int argc, char **argv)
{
    const char *cwd_str = get_cwd_flag(argc, argv);
    int found = 0;
    for (int i = 1; i < argc; i++) {
        if (is_shell_flag(argv[i])) continue;
        found++;
        char path[512];
        resolve_path(cwd_str, argv[i], path, sizeof(path));

        struct stat st;
        if (stat(path, &st) == 0) {
            if (st.st_type == DT_DIR)
                fprintf(stderr, "touch: cannot touch '%s': Is a directory\n", argv[i]);
            continue;
        }
        int fd = open(path, O_WRONLY | O_CREAT, 0644);
        if (fd < 0) {
            fprintf(stderr, "touch: cannot create '%s'\n", argv[i]);
            continue;
        }
        close(fd);
    }
    if (!found) {
        fputs("Usage: touch <file> [file2 ...]\n", stdout);
        return 1;
    }
    return 0;
}
