#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <cervus_util.h>

typedef struct {
    int l, a, A, d, F, h, one, R;
} ls_opts_t;

static void fmt_size(off_t sz, char *buf, int buflen)
{
    if      (sz >= 1024LL*1024*1024) snprintf(buf, buflen, "%3lldG", (long long)sz >> 30);
    else if (sz >= 1024LL*1024)      snprintf(buf, buflen, "%3lldM", (long long)sz >> 20);
    else if (sz >= 1024LL)           snprintf(buf, buflen, "%3lldK", (long long)sz >> 10);
    else                             snprintf(buf, buflen, "%3lld ", (long long)sz);
}

static void fmt_mode(mode_t m, uint32_t type, char *buf)
{
    if      (type == 1 || S_ISDIR(m))  buf[0] = 'd';
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
    char name[256];
    uint8_t d_type;
    struct stat st;
    int has_stat;
} Entry;

static int cmp_name(const void *a, const void *b)
{
    return strcmp(((const Entry *)a)->name, ((const Entry *)b)->name);
}

static char type_indicator(const Entry *e)
{
    if (e->d_type == DT_DIR) return '/';
    if (e->has_stat && (e->st.st_mode & S_IXUSR) && e->d_type != DT_DIR) return '*';
    return 0;
}

static void color_for(const Entry *e)
{
    if (e->d_type == DT_DIR) fputs(C_BLUE, stdout);
    else if (e->d_type == DT_CHR || e->d_type == DT_BLK) fputs(C_YELLOW, stdout);
    else if (e->has_stat && (e->st.st_mode & S_IXUSR)) fputs(C_GREEN, stdout);
}

static void emit_entry(const Entry *e, const ls_opts_t *o)
{
    if (o->l) {
        char mbuf[12];
        if (e->has_stat) fmt_mode(e->st.st_mode, e->st.st_type, mbuf);
        else strcpy(mbuf, "??????????");
        fputs(C_GRAY, stdout); fputs(mbuf, stdout); fputs(C_RESET "  ", stdout);

        char sbuf[16];
        if (e->has_stat && !S_ISDIR(e->st.st_mode)) {
            if (o->h) fmt_size(e->st.st_size, sbuf, sizeof(sbuf));
            else      snprintf(sbuf, sizeof(sbuf), "%7lld", (long long)e->st.st_size);
        } else {
            snprintf(sbuf, sizeof(sbuf), "%7s", "-");
        }
        fputs(C_CYAN, stdout); fputs(sbuf, stdout); fputs(C_RESET "  ", stdout);
    }
    color_for(e);
    fputs(e->name, stdout);
    fputs(C_RESET, stdout);
    if (o->F || o->l) {
        char ind = type_indicator(e);
        if (ind) putchar(ind);
    }
    putchar('\n');
}

static int list_dir(const char *path, const char *display, const ls_opts_t *o, int multiple)
{
    DIR *d = opendir(path);
    if (!d) { fprintf(stderr, "ls: cannot open '%s'\n", display); return 1; }

    Entry *entries = NULL;
    int count = 0, cap = 0;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (!o->a && !o->A && de->d_name[0] == '.') continue;
        if (o->A && (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)) continue;
        if (count >= cap) {
            cap = cap ? cap * 2 : 32;
            Entry *nb = (Entry *)realloc(entries, (size_t)cap * sizeof(Entry));
            if (!nb) { free(entries); closedir(d); fputs("ls: oom\n", stderr); return 1; }
            entries = nb;
        }
        Entry *e = &entries[count++];
        strncpy(e->name, de->d_name, 255);
        e->name[255] = '\0';
        e->d_type = de->d_type;
        e->has_stat = 0;
        if (o->l || o->F) {
            char full[512];
            path_join(path, e->name, full, sizeof(full));
            if (stat(full, &e->st) == 0) e->has_stat = 1;
        }
    }
    closedir(d);
    if (count > 1) qsort(entries, (size_t)count, sizeof(Entry), cmp_name);

    if (multiple) { fputs(display, stdout); fputs(":\n", stdout); }
    for (int i = 0; i < count; i++) emit_entry(&entries[i], o);
    if (multiple) putchar('\n');
    free(entries);
    return 0;
}

static const char USAGE[] =
    "Usage: ls [-1ACFRadhl] [file ...]\nList directory contents.\n\n  -1   one entry per line\n  -a   show hidden files (starting with .)\n  -A   like -a but without . and ..\n  -C   multi-column (default)\n  -d   list directories themselves, not contents\n  -F   classify entries (append /, *, etc.)\n  -h   human-readable sizes (with -l)\n  -l   long listing\n  -R   recursive (per dir block)\n";

static void usage(void) { fputs(USAGE, stderr); }

int main(int argc, char **argv)
{
    if (cervus_check_help_version(argc, argv, USAGE, "ls")) return 0;
    const char *cwd = get_cwd_flag(argc, argv);
    argc = cervus_filter_args(argc, argv);

    ls_opts_t o;
    memset(&o, 0, sizeof(o));

    int opt;
    while ((opt = getopt(argc, argv, "1ACFRadhl")) != -1) {
        switch (opt) {
            case '1': o.one = 1; break;
            case 'A': o.A = 1; break;
            case 'C': break;
            case 'F': o.F = 1; break;
            case 'R': o.R = 1; break;
            case 'a': o.a = 1; break;
            case 'd': o.d = 1; break;
            case 'h': o.h = 1; break;
            case 'l': o.l = 1; break;
            default: usage(); return 1;
        }
    }

    int npaths = argc - optind;
    int rc = 0;

    if (npaths == 0) {
        rc = list_dir(cwd, ".", &o, 0);
        return rc;
    }

    int multiple = npaths > 1;
    for (int i = optind; i < argc; i++) {
        char resolved[512];
        resolve_path(cwd, argv[i], resolved, sizeof(resolved));
        struct stat st;
        if (stat(resolved, &st) != 0) {
            fprintf(stderr, "ls: cannot access '%s'\n", argv[i]);
            rc = 1; continue;
        }
        if (st.st_type == 1 && !o.d) {
            if (list_dir(resolved, argv[i], &o, multiple) != 0) rc = 1;
        } else {
            Entry e;
            strncpy(e.name, argv[i], 255); e.name[255] = '\0';
            e.d_type = st.st_type;
            e.st = st;
            e.has_stat = 1;
            emit_entry(&e, &o);
        }
    }
    return rc;
}
