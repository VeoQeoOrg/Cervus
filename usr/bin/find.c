#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <cervus_util.h>

typedef struct {
    const char *name_pat;
    char type;
    int  maxdepth;
} find_opts_t;

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

static int type_match(uint8_t d_type, char tc)
{
    switch (tc) {
        case 'f': return d_type == DT_REG || d_type == 0;
        case 'd': return d_type == DT_DIR;
        case 'l': return d_type == DT_LNK;
        case 'c': return d_type == DT_CHR;
        case 'b': return d_type == DT_BLK;
        case 'p': return d_type == DT_FIFO;
        default:  return 1;
    }
}

static void do_find(const char *dir, const find_opts_t *o, int depth)
{
    if (o->maxdepth >= 0 && depth > o->maxdepth) return;
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.' &&
            (de->d_name[1] == '\0' ||
             (de->d_name[1] == '.' && de->d_name[2] == '\0'))) continue;
        char path[512];
        path_join(dir, de->d_name, path, sizeof(path));
        int match = 1;
        if (o->name_pat && !str_match(de->d_name, o->name_pat)) match = 0;
        if (o->type && !type_match(de->d_type, o->type)) match = 0;
        if (match) {
            fputs(path, stdout);
            if (de->d_type == DT_DIR) putchar('/');
            putchar('\n');
        }
        if (de->d_type == DT_DIR) do_find(path, o, depth + 1);
    }
    closedir(d);
}

static const char USAGE[] =
    "Usage: find [path ...] [-name pat] [-type c] [-maxdepth N]\nRecursively walk the directory tree.\n\n  -name PAT     match basename against shell glob PAT (* ?)\n  -type c       filter by type: f file, d dir, l link, c chr, b blk, p pipe\n  -maxdepth N   descend at most N levels\n  -print        explicit print (default action)\n";

static void usage(void) { fputs(USAGE, stderr); }

int main(int argc, char **argv)
{
    if (cervus_check_help_version(argc, argv, USAGE, "find")) return 0;
    const char *cwd = get_cwd_flag(argc, argv);
    argc = cervus_filter_args(argc, argv);

    find_opts_t o;
    memset(&o, 0, sizeof(o));
    o.maxdepth = -1;

    const char *paths[16];
    int npaths = 0;

    int i = 1;
    for (; i < argc; i++) {
        if (argv[i][0] == '-') break;
        if (npaths < 16) paths[npaths++] = argv[i];
    }
    for (; i < argc; i++) {
        if (strcmp(argv[i], "-name") == 0) {
            if (i + 1 >= argc) { usage(); return 1; }
            o.name_pat = argv[++i];
        } else if (strcmp(argv[i], "-type") == 0) {
            if (i + 1 >= argc) { usage(); return 1; }
            o.type = argv[++i][0];
        } else if (strcmp(argv[i], "-maxdepth") == 0) {
            if (i + 1 >= argc) { usage(); return 1; }
            o.maxdepth = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-print") == 0) {
            /* default action */
        } else {
            fprintf(stderr, "find: unknown predicate '%s'\n", argv[i]);
            return 1;
        }
    }

    if (npaths == 0) { paths[0] = "."; npaths = 1; }

    for (int p = 0; p < npaths; p++) {
        char resolved[512];
        resolve_path(cwd, paths[p], resolved, sizeof(resolved));
        struct stat st;
        if (stat(resolved, &st) != 0) {
            fprintf(stderr, "find: '%s': no such file or directory\n", paths[p]);
            continue;
        }
        if (st.st_type != DT_DIR) { fputs(resolved, stdout); putchar('\n'); continue; }
        fputs(resolved, stdout); putchar('\n');
        do_find(resolved, &o, 1);
    }
    return 0;
}
