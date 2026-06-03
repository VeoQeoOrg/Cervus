#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <dirent.h>
#include <regex.h>
#include <sys/stat.h>
#include <cervus_util.h>

typedef struct {
    int ignore_case;
    int invert;
    int show_lineno;
    int recursive;
    int count_only;
    int files_with_match;
    int quiet;
    int no_filename;
    int fixed;
    int extended;
    regex_t re;
    int re_ready;
} grep_opts_t;

static int match_line(const char *line, const char *pat, grep_opts_t *o)
{
    if (o->fixed) {
        size_t pl = strlen(pat);
        if (pl == 0) return 1;
        for (const char *p = line; *p; p++) {
            size_t k = 0;
            while (k < pl && p[k]) {
                char a = p[k], b = pat[k];
                if (o->ignore_case) {
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
    if (!o->re_ready) return 0;
    return regexec(&o->re, line, 0, NULL, 0) == 0;
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
                   const grep_opts_t *o, int *any, int skip_binary)
{
    char buf[8192];
    int line = 0;
    int matched_in_file = 0;
    char acc[4096];
    int alen = 0;
    int checked = 0, is_binary = 0;

    while (1) {
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n <= 0) break;
        if (skip_binary && !checked) { checked = 1; if (looks_binary(buf, n)) { is_binary = 1; break; } }
        for (ssize_t i = 0; i < n; i++) {
            char c = buf[i];
            if (c == '\n' || alen + 1 >= (int)sizeof(acc)) {
                acc[alen] = '\0';
                line++;
                int m = match_line(acc, pat, (grep_opts_t *)o);
                if (o->invert) m = !m;
                if (m) {
                    matched_in_file++;
                    if (any) *any = 1;
                    if (!o->quiet && !o->count_only && !o->files_with_match) {
                        if (prefix && !o->no_filename) { fputs(prefix, stdout); fputs(":", stdout); }
                        if (o->show_lineno) fprintf(stdout, "%d:", line);
                        fputs(acc, stdout);
                        putchar('\n');
                    }
                    if (o->quiet) return 0;
                    if (o->files_with_match) {
                        if (prefix) { puts(prefix); }
                        return 0;
                    }
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
        int m = match_line(acc, pat, (grep_opts_t *)o);
        if (o->invert) m = !m;
        if (m) {
            matched_in_file++;
            if (any) *any = 1;
            if (!o->quiet && !o->count_only && !o->files_with_match) {
                if (prefix && !o->no_filename) { fputs(prefix, stdout); fputs(":", stdout); }
                if (o->show_lineno) fprintf(stdout, "%d:", line);
                fputs(acc, stdout);
                putchar('\n');
            }
        }
    }

    if (o->count_only) {
        if (prefix && !o->no_filename) { fputs(prefix, stdout); fputs(":", stdout); }
        printf("%d\n", matched_in_file);
    }
    return matched_in_file > 0 ? 0 : 1;
}

static int grep_path(const char *path, const char *pat,
                     const grep_opts_t *o, int show_prefix, int *any);

static int grep_dir(const char *dir, const char *pat,
                    const grep_opts_t *o, int *any)
{
    DIR *d = opendir(dir);
    if (!d) { fprintf(stderr, "grep: cannot open directory '%s'\n", dir); return 1; }
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        const char *nm = de->d_name;
        if (nm[0] == '.' && (nm[1] == '\0' || (nm[1] == '.' && nm[2] == '\0'))) continue;
        char child[512];
        snprintf(child, sizeof(child), "%s/%s", dir, nm);
        grep_path(child, pat, o, 1, any);
    }
    closedir(d);
    return 0;
}

static int grep_path(const char *path, const char *pat,
                     const grep_opts_t *o, int show_prefix, int *any)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        fprintf(stderr, "grep: '%s': no such file or directory\n", path);
        return 1;
    }
    if (st.st_type == DT_DIR) {
        if (!o->recursive) { fprintf(stderr, "grep: '%s': is a directory (use -r)\n", path); return 1; }
        return grep_dir(path, pat, o, any);
    }
    int fd = open(path, O_RDONLY);
    if (fd < 0) { fprintf(stderr, "grep: cannot open '%s'\n", path); return 1; }
    const char *prefix = show_prefix ? path : NULL;
    grep_fd(fd, pat, prefix, o, any, 1);
    close(fd);
    return 0;
}

static const char USAGE[] =
    "Usage: grep [-cEFHhilnqrRsvx] [-e pattern] pattern [file ...]\nSearch for PATTERN in each FILE.\n\n  -c   only print count of matching lines\n  -e   pattern (allows pattern beginning with -)\n  -H   print filename prefix\n  -h   suppress filename prefix\n  -i   ignore case\n  -l   list filenames with matches only\n  -n   prefix each line with line number\n  -q   quiet, exit 0 on first match\n  -r, -R  recurse into directories\n  -v   invert match\n";

static void usage(void) { fputs(USAGE, stderr); }

int main(int argc, char **argv)
{
    if (cervus_check_help_version(argc, argv, USAGE, "grep")) return 0;

    grep_opts_t o;
    memset(&o, 0, sizeof(o));
    const char *e_pat = NULL;

    int opt;
    while ((opt = getopt(argc, argv, "cEFHe:hilnqrRsvx")) != -1) {
        switch (opt) {
            case 'c': o.count_only = 1; break;
            case 'E': o.extended = 1; break;
            case 'F': o.fixed = 1; break;
            case 'H': o.no_filename = 0; break;
            case 'e': e_pat = optarg; break;
            case 'h': o.no_filename = 1; break;
            case 'i': o.ignore_case = 1; break;
            case 'l': o.files_with_match = 1; break;
            case 'n': o.show_lineno = 1; break;
            case 'q': o.quiet = 1; break;
            case 'r': case 'R': o.recursive = 1; break;
            case 's': break;
            case 'v': o.invert = 1; break;
            case 'x': break;
            default: usage(); return 2;
        }
    }

    const char *pat = e_pat;
    int file_start = optind;
    if (!pat) {
        if (optind >= argc) { usage(); return 2; }
        pat = argv[optind];
        file_start = optind + 1;
    }

    if (!o.fixed) {
        int cflags = 0;
        if (o.extended) cflags |= REG_EXTENDED;
        if (o.ignore_case) cflags |= REG_ICASE;
        cflags |= REG_NOSUB;
        int rc = regcomp(&o.re, pat, cflags);
        if (rc != 0) {
            char err[128];
            regerror(rc, &o.re, err, sizeof(err));
            fprintf(stderr, "grep: bad pattern: %s\n", err);
            return 2;
        }
        o.re_ready = 1;
    }

    int any = 0;
    int nf = argc - file_start;

    int rc_final = 0;
    if (o.recursive && nf == 0) {
        grep_path(".", pat, &o, 1, &any);
        rc_final = any ? 0 : 1;
        goto out;
    }

    if (nf == 0) {
        grep_fd(0, pat, NULL, &o, &any, 0);
        rc_final = any ? 0 : 1;
        goto out;
    }

    int show_prefix = (nf > 1 || o.recursive) && !o.no_filename;
    for (int i = file_start; i < argc; i++) {
        char resolved[512];
        snprintf(resolved, sizeof(resolved), "%s", argv[i]);
        grep_path(resolved, pat, &o, show_prefix, &any);
    }
    rc_final = any ? 0 : 1;
out:
    if (o.re_ready) regfree(&o.re);
    return rc_final;
}
