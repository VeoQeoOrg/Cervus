#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ftw.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cervus_util.h>

static const char USAGE[] =
    "Usage: chmod [-R] mode file ...\n"
    "Change file permissions.\n\n"
    "  -R    change files and directories recursively\n\n"
    "MODE is octal (644) or symbolic: [ugoa]*[+-=][rwxXst]* or [+-=][ugo],\n"
    "comma separated, for example u+x, go-w, a=rX, u=g.\n";

static void *g_set;
static int g_rc;

static int apply(const char *path, const struct stat *st)
{
    mode_t m = getmode(g_set, st->st_mode);
    if (chmod(path, m & 07777) < 0) {
        fprintf(stderr, "chmod: cannot change '%s': %s\n", path, strerror(errno));
        g_rc = 1;
    }
    return 0;
}

static int visit(const char *path, const struct stat *st, int type, struct FTW *f)
{
    (void)f;
    if (type == FTW_SL) return 0;
    if (type == FTW_NS) {
        fprintf(stderr, "chmod: cannot access '%s'\n", path);
        g_rc = 1;
        return 0;
    }
    if (type == FTW_DNR) fprintf(stderr, "chmod: cannot read directory '%s'\n", path), g_rc = 1;
    return apply(path, st);
}

int main(int argc, char **argv)
{
    if (cervus_check_help_version(argc, argv, USAGE, "chmod")) return 0;
    int recursive = 0, i = 1;
    for (; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        if (!strcmp(argv[i], "--")) { i++; break; }
        if (!strcmp(argv[i], "-R")) { recursive = 1; continue; }
        if (strspn(argv[i] + 1, "rwxXstugo") == strlen(argv[i] + 1) || strchr(argv[i] + 1, ',')) break;
        fprintf(stderr, "chmod: unknown option '%s'\n", argv[i]);
        fputs(USAGE, stderr);
        return 2;
    }
    if (argc - i < 2) { fputs(USAGE, stderr); return 2; }
    g_set = setmode(argv[i]);
    if (!g_set) { fprintf(stderr, "chmod: invalid mode '%s'\n", argv[i]); return 2; }

    for (i++; i < argc; i++) {
        struct stat st;
        if (stat(argv[i], &st) < 0) {
            fprintf(stderr, "chmod: cannot access '%s': %s\n", argv[i], strerror(errno));
            g_rc = 1;
            continue;
        }
        if (recursive && S_ISDIR(st.st_mode)) {
            if (nftw(argv[i], visit, 32, FTW_PHYS) < 0) {
                fprintf(stderr, "chmod: cannot walk '%s': %s\n", argv[i], strerror(errno));
                g_rc = 1;
            }
        } else {
            apply(argv[i], &st);
        }
    }
    free(g_set);
    return g_rc;
}
