#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include <cervus_util.h>

static int match_line(const char *line, const char *pat, int ignore_case)
{
    size_t pl = strlen(pat);
    if (pl == 0) return 1;
    for (const char *p = line; *p; p++) {
        size_t k = 0;
        while (k < pl && p[k]) {
            char a = p[k], b = pat[k];
            if (ignore_case) {
                if (a >= 'A' && a <= 'Z') a = a - 'A' + 'a';
                if (b >= 'A' && b <= 'Z') b = b - 'A' + 'a';
            }
            if (a != b) break;
            k++;
        }
        if (k == pl) return 1;
    }
    return 0;
}

static int looks_binary(const char *buf, ssize_t n)
{
    int nonprint = 0;
    for (ssize_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)buf[i];
        if (c == 0x00) return 1;
        if (c < 0x08 || (c > 0x0d && c < 0x20 && c != 0x1b)) nonprint++;
    }
    return (n > 0 && nonprint * 100 / n > 30);
}

static int grep_fd(int fd, const char *pat, const char *prefix,
                   int ignore_case, int invert, int show_lineno, int *any,
                   int skip_binary)
{
    char buf[8192];
    int line = 0;
    int rc = 0;
    char acc[4096];
    int alen = 0;
    int checked_binary = 0;
    int is_binary = 0;

    while (1) {
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n <= 0) break;
        if (skip_binary && !checked_binary) {
            checked_binary = 1;
            if (looks_binary(buf, n)) { is_binary = 1; break; }
        }
        for (ssize_t i = 0; i < n; i++) {
            char c = buf[i];
            if (c == '\n' || alen + 1 >= (int)sizeof(acc)) {
                acc[alen] = '\0';
                line++;
                int m = match_line(acc, pat, ignore_case);
                if (invert) m = !m;
                if (m) {
                    if (any) *any = 1;
                    if (prefix) { fputs(prefix, stdout); fputs(":", stdout); }
                    if (show_lineno) { fprintf(stdout, "%d:", line); }
                    fputs(acc, stdout);
                    putchar('\n');
                    rc = 0;
                }
                alen = 0;
                if (c != '\n') acc[alen++] = c;
            } else {
                acc[alen++] = c;
            }
        }
    }
    if (alen > 0 && !is_binary) {
        acc[alen] = '\0';
        line++;
        int m = match_line(acc, pat, ignore_case);
        if (invert) m = !m;
        if (m) {
            if (any) *any = 1;
            if (prefix) { fputs(prefix, stdout); fputs(":", stdout); }
            if (show_lineno) { fprintf(stdout, "%d:", line); }
            fputs(acc, stdout);
            putchar('\n');
        }
    }
    return rc;
}

static int grep_path(const char *path, const char *pat,
                     int ignore_case, int invert, int show_lineno,
                     int recursive, int show_prefix, int *any);

static int grep_dir(const char *dir, const char *pat,
                    int ignore_case, int invert, int show_lineno, int *any)
{
    DIR *d = opendir(dir);
    if (!d) {
        fprintf(stderr, "grep: cannot open directory '%s'\n", dir);
        return 1;
    }
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        const char *nm = de->d_name;
        if (nm[0] == '.' && (nm[1] == '\0' || (nm[1] == '.' && nm[2] == '\0'))) continue;
        char child[512];
        snprintf(child, sizeof(child), "%s/%s", dir, nm);
        grep_path(child, pat, ignore_case, invert, show_lineno, 1, 1, any);
    }
    closedir(d);
    return 0;
}

static int grep_path(const char *path, const char *pat,
                     int ignore_case, int invert, int show_lineno,
                     int recursive, int show_prefix, int *any)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        fprintf(stderr, "grep: '%s': no such file or directory\n", path);
        return 1;
    }
    if (st.st_type == DT_DIR) {
        if (!recursive) {
            fprintf(stderr, "grep: '%s': is a directory (use -r)\n", path);
            return 1;
        }
        return grep_dir(path, pat, ignore_case, invert, show_lineno, any);
    }
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "grep: cannot open '%s'\n", path);
        return 1;
    }
    const char *prefix = show_prefix ? path : NULL;
    grep_fd(fd, pat, prefix, ignore_case, invert, show_lineno, any, 1);
    close(fd);
    return 0;
}

int main(int argc, char **argv)
{
    const char *cwd = get_cwd_flag(argc, argv);
    int ignore_case = 0, invert = 0, show_lineno = 0, recursive = 0;
    const char *pat = NULL;
    const char *files[64];
    int nf = 0;

    for (int i = 1; i < argc; i++) {
        if (is_shell_flag(argv[i])) continue;
        if (argv[i][0] == '-' && argv[i][1] != '\0' && argv[i][1] != '-') {
            for (const char *f = argv[i] + 1; *f; f++) {
                if (*f == 'i') ignore_case = 1;
                else if (*f == 'v') invert = 1;
                else if (*f == 'n') show_lineno = 1;
                else if (*f == 'r' || *f == 'R') recursive = 1;
                else if (*f == 'F') { }
                else if (*f == 'H') { }
            }
            continue;
        }
        if (!pat) pat = argv[i];
        else if (nf < 64) files[nf++] = argv[i];
    }

    if (!pat) {
        fputs(C_RED "usage: grep [-ivnr] <pattern> [<file>|<dir>...]" C_RESET "\n", stderr);
        return 2;
    }

    int found = 0;

    if (recursive && nf == 0) {
        files[nf++] = ".";
    }

    if (nf == 0) {
        grep_fd(0, pat, NULL, ignore_case, invert, show_lineno, &found, 0);
        return found ? 0 : 1;
    }

    int show_prefix = (nf > 1) || recursive;
    for (int i = 0; i < nf; i++) {
        char resolved[512];
        resolve_path(cwd, files[i], resolved, sizeof(resolved));
        grep_path(resolved, pat, ignore_case, invert, show_lineno,
                  recursive, show_prefix, &found);
    }
    return found ? 0 : 1;
}
