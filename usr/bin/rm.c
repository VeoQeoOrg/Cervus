#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <dirent.h>
#include <cervus_util.h>

typedef struct {
    int recursive;
    int force;
    int interactive;
    int verbose;
} rm_opts_t;

static int remove_tree(const char *path, const rm_opts_t *o)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        if (o->force) return 0;
        fprintf(stderr, "rm: cannot remove '%s': No such file or directory\n", path);
        return 1;
    }
    if (st.st_type != DT_DIR) {
        if (o->interactive) {
            fprintf(stderr, "rm: remove '%s'? ", path);
            char ans[8];
            ssize_t r = read(0, ans, sizeof(ans) - 1);
            if (r <= 0 || (ans[0] != 'y' && ans[0] != 'Y')) return 0;
        }
        if (unlink(path) != 0) {
            if (!o->force) fprintf(stderr, "rm: cannot remove '%s'\n", path);
            return o->force ? 0 : 1;
        }
        if (o->verbose) fprintf(stderr, "removed '%s'\n", path);
        return 0;
    }

    if (!o->recursive) {
        fprintf(stderr, "rm: '%s': is a directory (use -r)\n", path);
        return 1;
    }
    DIR *d = opendir(path);
    if (!d) {
        if (!o->force) fprintf(stderr, "rm: cannot open directory '%s'\n", path);
        return o->force ? 0 : 1;
    }
    struct dirent *de;
    int rc = 0;
    while ((de = readdir(d)) != NULL) {
        const char *nm = de->d_name;
        if (nm[0] == '.' && (nm[1] == '\0' || (nm[1] == '.' && nm[2] == '\0'))) continue;
        char child[512];
        snprintf(child, sizeof(child), "%s/%s", path, nm);
        if (remove_tree(child, o) != 0) rc = 1;
    }
    closedir(d);
    if (rmdir(path) != 0) {
        if (!o->force) fprintf(stderr, "rm: cannot remove directory '%s'\n", path);
        return o->force ? rc : 1;
    }
    if (o->verbose) fprintf(stderr, "removed directory '%s'\n", path);
    return rc;
}

static const char USAGE[] =
    "Usage: rm [-fiRrv] file ...\nRemove (unlink) the FILE(s).\n\n  -f   ignore nonexistent files, never prompt\n  -i   prompt before each removal\n  -R, -r  remove directories and their contents\n  -v   verbose\n";

static void usage(void) { fputs(USAGE, stderr); }

int main(int argc, char **argv)
{
    if (cervus_check_help_version(argc, argv, USAGE, "rm")) return 0;

    rm_opts_t o;
    memset(&o, 0, sizeof(o));

    int opt;
    while ((opt = getopt(argc, argv, "fiRrv")) != -1) {
        switch (opt) {
            case 'f': o.force = 1; o.interactive = 0; break;
            case 'i': o.interactive = 1; o.force = 0; break;
            case 'R': case 'r': o.recursive = 1; break;
            case 'v': o.verbose = 1; break;
            default: usage(); return 1;
        }
    }

    if (optind >= argc) {
        if (!o.force) usage();
        return o.force ? 0 : 1;
    }

    int rc = 0;
    for (int i = optind; i < argc; i++) {
        char path[512];
        snprintf(path, sizeof(path), "%s", argv[i]);
        const char *danger = cervus_path_danger(path);
        if (danger) {
            if (!cervus_confirm("remove", path, danger)) {
                fprintf(stderr, "rm: skipping '%s'\n", argv[i]);
                continue;
            }
        }
        if (remove_tree(path, &o) != 0) rc = 1;
    }
    return rc;
}
