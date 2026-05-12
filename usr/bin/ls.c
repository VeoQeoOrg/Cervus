#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <cervus_util.h>

static void fmt_size(off_t sz, char *buf, int buflen)
{
    if      (sz >= 1024LL*1024*1024) snprintf(buf, buflen, "%3lldG", (long long)sz >> 30);
    else if (sz >= 1024LL*1024)      snprintf(buf, buflen, "%3lldM", (long long)sz >> 20);
    else if (sz >= 1024LL)           snprintf(buf, buflen, "%3lldK", (long long)sz >> 10);
    else                             snprintf(buf, buflen, "%3lld ", (long long)sz);
}

static void fmt_mode(mode_t m, uint32_t type, char *buf)
{
    if      (type == 4 || S_ISDIR(m))  buf[0] = 'd';
    else if (S_ISCHR(m))               buf[0] = 'c';
    else if (S_ISBLK(m))               buf[0] = 'b';
    else if (S_ISLNK(m))               buf[0] = 'l';
    else if (S_ISFIFO(m))              buf[0] = 'p';
    else                               buf[0] = '-';

    const char *bits = "rwxrwxrwx";
    for (int i = 0; i < 9; i++)
        buf[1 + i] = (m & (0400 >> i)) ? bits[i] : '-';
    buf[10] = '\0';
}

typedef struct {
    char      name[256];
    uint8_t   d_type;
    struct stat st;
    int       has_stat;
} Entry;

static int cmp_name(const void *a, const void *b)
{
    return strcmp(((const Entry *)a)->name, ((const Entry *)b)->name);
}

int main(int argc, char **argv)
{
    int flag_l    = 0;
    int flag_a    = 0;
    int flag_h    = 0;
    int flag_1    = 0;
    int flag_help = 0;

    const char *cwd  = get_cwd_flag(argc, argv);
    const char *path = NULL;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (is_shell_flag(a)) continue;

        if (strcmp(a, "--help") == 0) { flag_help = 1; continue; }
        if (strcmp(a, "--")    == 0) continue;

        if (a[0] == '-' && a[1] != '\0') {
            for (const char *f = a + 1; *f; f++) {
                switch (*f) {
                    case 'l': flag_l = 1; break;
                    case 'a': flag_a = 1; break;
                    case 'h': flag_h = 1; break;
                    case '1': flag_1 = 1; break;
                    default:
                        fprintf(stderr, "ls: unknown flag '-%c'\n", *f);
                        return 1;
                }
            }
            continue;
        }

        if (!path) path = a;
    }

    if (flag_help) {
        fputs(
            "Usage: ls [OPTION]... [PATH]\n"
            "List directory contents.\n"
            "\n"
            "  -l        long listing: permissions, size, name\n"
            "  -a        show hidden files (starting with '.')\n"
            "  -h        human-readable sizes (K, M, G) — use with -l\n"
            "  -1        one entry per line\n"
            "  --help    display this help and exit\n"
            "\n"
            "Entries are sorted alphabetically. Directories are shown in "
            C_BLUE "blue" C_RESET ", devices in " C_YELLOW "yellow" C_RESET ".\n",
            stdout
        );
        return 0;
    }

    char resolved[512];
    resolve_path(cwd, path ? path : cwd, resolved, sizeof(resolved));

    DIR *d = opendir(resolved);
    if (!d) {
        fprintf(stderr, "ls: cannot open: %s\n", path ? path : resolved);
        return 1;
    }

    Entry  *entries = NULL;
    int     count   = 0;
    int     cap     = 0;

    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (!flag_a && de->d_name[0] == '.') continue;

        if (count >= cap) {
            cap = cap ? cap * 2 : 32;
            entries = (Entry *)realloc(entries, (size_t)cap * sizeof(Entry));
            if (!entries) { fputs("ls: out of memory\n", stderr); return 1; }
        }

        Entry *e = &entries[count++];
        strncpy(e->name, de->d_name, 255);
        e->name[255] = '\0';
        e->d_type    = de->d_type;
        e->has_stat  = 0;

        if (flag_l) {
            char full[512];
            path_join(resolved, e->name, full, sizeof(full));
            if (stat(full, &e->st) == 0) e->has_stat = 1;
        }
    }
    closedir(d);

    if (count > 1)
        qsort(entries, (size_t)count, sizeof(Entry), cmp_name);

    putchar('\n');

    if (!count) {
        fputs("  " C_GRAY "(empty)" C_RESET "\n", stdout);
    } else if (flag_l) {
        for (int i = 0; i < count; i++) {
            Entry *e = &entries[i];

            if (e->has_stat) {
                char mbuf[12];
                fmt_mode(e->st.st_mode, e->st.st_type, mbuf);
                fputs("  ", stdout);
                fputs(C_GRAY, stdout); fputs(mbuf, stdout); fputs(C_RESET, stdout);
            } else {
                fputs("  " C_GRAY "??????????" C_RESET, stdout);
            }

            if (e->has_stat && !S_ISDIR(e->st.st_mode)) {
                char sbuf[8];
                if (flag_h) fmt_size(e->st.st_size, sbuf, sizeof(sbuf));
                else        snprintf(sbuf, sizeof(sbuf), "%7lld", (long long)e->st.st_size);
                fputs("  ", stdout);
                fputs(C_CYAN, stdout); fputs(sbuf, stdout); fputs(C_RESET, stdout);
            } else {
                fputs("        -", stdout);
            }

            fputs("  ", stdout);
            if      (e->d_type == DT_DIR) fputs(C_BLUE, stdout);
            else if (e->d_type == DT_CHR || e->d_type == DT_BLK) fputs(C_YELLOW, stdout);
            else if (e->has_stat && (e->st.st_mode & S_IXUSR)) fputs(C_GREEN, stdout);
            fputs(e->name, stdout);
            fputs(C_RESET, stdout);
            if (e->d_type == DT_DIR) putchar('/');
            putchar('\n');
        }
    } else if (flag_1) {
        for (int i = 0; i < count; i++) {
            Entry *e = &entries[i];
            fputs("  ", stdout);
            if      (e->d_type == DT_DIR) fputs(C_BLUE, stdout);
            else if (e->d_type == DT_CHR || e->d_type == DT_BLK) fputs(C_YELLOW, stdout);
            fputs(e->name, stdout);
            fputs(C_RESET, stdout);
            if (e->d_type == DT_DIR) putchar('/');
            putchar('\n');
        }
    } else {
        for (int i = 0; i < count; i++) {
            Entry *e = &entries[i];
            fputs("  ", stdout);
            if      (e->d_type == DT_DIR) fputs(C_BLUE, stdout);
            else if (e->d_type == DT_CHR || e->d_type == DT_BLK) fputs(C_YELLOW, stdout);
            fputs(e->name, stdout);
            fputs(C_RESET, stdout);
            if (e->d_type == DT_DIR) putchar('/');
            putchar('\n');
        }
    }

    putchar('\n');
    free(entries);
    return 0;
}