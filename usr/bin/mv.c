#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cervus_util.h>

static const char *path_basename(const char *p)
{
    const char *b = p;
    for (const char *q = p; *q; q++) if (*q == '/') b = q + 1;
    return b;
}

static const char USAGE[] =
    "Usage: mv [-fiv] source ... target\nRename SOURCE to DEST, or move SOURCE(s) into DIRECTORY.\n\n  -f   never prompt\n  -i   prompt before overwrite\n  -v   verbose\n";

static void usage(void) { fputs(USAGE, stderr); }

int main(int argc, char **argv)
{
    if (cervus_check_help_version(argc, argv, USAGE, "mv")) return 0;
    const char *cwd = get_cwd_flag(argc, argv);
    argc = cervus_filter_args(argc, argv);

    int force = 0, interactive = 0, verbose = 0;
    int opt;
    while ((opt = getopt(argc, argv, "fiv")) != -1) {
        switch (opt) {
            case 'f': force = 1; interactive = 0; break;
            case 'i': interactive = 1; force = 0; break;
            case 'v': verbose = 1; break;
            default: usage(); return 1;
        }
    }

    int nf = argc - optind;
    if (nf < 2) { usage(); return 1; }

    const char *target = argv[argc - 1];
    char dst_resolved[512];
    resolve_path(cwd, target, dst_resolved, sizeof(dst_resolved));

    struct stat dst_st;
    int dst_is_dir = (stat(dst_resolved, &dst_st) == 0 && dst_st.st_type == 1);
    if (nf > 2 && !dst_is_dir) {
        fprintf(stderr, "mv: target '%s' is not a directory\n", target);
        return 1;
    }

    int rc = 0;
    for (int i = optind; i < argc - 1; i++) {
        char sp[512], dp[512];
        resolve_path(cwd, argv[i], sp, sizeof(sp));
        if (dst_is_dir) {
            snprintf(dp, sizeof(dp), "%s/%s", dst_resolved, path_basename(argv[i]));
        } else {
            strncpy(dp, dst_resolved, sizeof(dp) - 1);
            dp[sizeof(dp) - 1] = '\0';
        }

        const char *src_danger = cervus_path_danger(sp);
        if (src_danger) {
            if (!cervus_confirm("move source", sp, src_danger)) {
                fprintf(stderr, "mv: skipping '%s'\n", argv[i]);
                continue;
            }
        }

        struct stat tst;
        if (stat(dp, &tst) == 0) {
            if (interactive) {
                fprintf(stderr, "mv: overwrite '%s'? ", argv[i]);
                char ans[8];
                ssize_t r = read(0, ans, sizeof(ans) - 1);
                if (r <= 0 || (ans[0] != 'y' && ans[0] != 'Y')) continue;
            }
            if (force) unlink(dp);
        }

        if (rename(sp, dp) < 0) {
            fprintf(stderr, "mv: cannot move '%s' to '%s'\n", argv[i], dp);
            rc = 1; continue;
        }
        if (verbose) fprintf(stderr, "'%s' -> '%s'\n", argv[i], dp);
    }
    return rc;
}
