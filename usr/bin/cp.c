#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <cervus_util.h>

typedef struct {
    int recursive;
    int interactive;
    int force;
    int verbose;
    int preserve;
} cp_opts_t;

static int copy_file(const char *src, const char *dst, const cp_opts_t *o)
{
    struct stat dst_st;
    int dst_exists = (stat(dst, &dst_st) == 0);
    if (dst_exists && o->interactive) {
        fprintf(stderr, "cp: overwrite '%s'? ", dst);
        char ans[8];
        ssize_t n = read(0, ans, sizeof(ans) - 1);
        if (n <= 0 || (ans[0] != 'y' && ans[0] != 'Y')) return 0;
    }
    int sfd = open(src, O_RDONLY);
    if (sfd < 0) { fprintf(stderr, "cp: cannot open '%s'\n", src); return 1; }
    int dfd = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (dfd < 0) {
        if (o->force) {
            unlink(dst);
            dfd = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        }
        if (dfd < 0) { fprintf(stderr, "cp: cannot create '%s'\n", dst); close(sfd); return 1; }
    }
    char buf[4096];
    ssize_t n;
    int rc = 0;
    while ((n = read(sfd, buf, sizeof(buf))) > 0) {
        ssize_t off = 0;
        while (off < n) {
            ssize_t w = write(dfd, buf + off, (size_t)(n - off));
            if (w <= 0) { fprintf(stderr, "cp: write error to '%s'\n", dst); rc = 1; break; }
            off += w;
        }
        if (rc) break;
    }
    if (n < 0) { fprintf(stderr, "cp: read error from '%s'\n", src); rc = 1; }
    close(sfd);
    close(dfd);
    if (rc == 0 && o->verbose) fprintf(stderr, "'%s' -> '%s'\n", src, dst);
    return rc;
}

static int copy_tree(const char *src, const char *dst, const cp_opts_t *o)
{
    struct stat st;
    if (stat(src, &st) != 0) { fprintf(stderr, "cp: '%s': no such file\n", src); return 1; }
    if (st.st_type != 1) return copy_file(src, dst, o);

    if (stat(dst, &st) != 0) {
        if (mkdir(dst, 0755) != 0) { fprintf(stderr, "cp: cannot create directory '%s'\n", dst); return 1; }
        if (o->verbose) fprintf(stderr, "'%s' -> '%s'\n", src, dst);
    }
    DIR *d = opendir(src);
    if (!d) { fprintf(stderr, "cp: cannot open directory '%s'\n", src); return 1; }
    struct dirent *de;
    int rc = 0;
    while ((de = readdir(d)) != NULL) {
        const char *nm = de->d_name;
        if (nm[0] == '.' && (nm[1] == '\0' || (nm[1] == '.' && nm[2] == '\0'))) continue;
        char sp[512], dp[512];
        snprintf(sp, sizeof(sp), "%s/%s", src, nm);
        snprintf(dp, sizeof(dp), "%s/%s", dst, nm);
        if (copy_tree(sp, dp, o) != 0) rc = 1;
    }
    closedir(d);
    return rc;
}

static const char *path_basename(const char *p)
{
    const char *b = p;
    for (const char *q = p; *q; q++) if (*q == '/') b = q + 1;
    return b;
}

static const char USAGE[] =
    "Usage: cp [-fiprRv] source ... target\nCopy SOURCE to DEST, or multiple SOURCE(s) into DIRECTORY.\n\n  -f   force overwrite without prompt\n  -i   prompt before overwrite\n  -p   preserve attributes (currently no-op)\n  -R, -r  recursively copy directories\n  -v   verbose\n";

static void usage(void) { fputs(USAGE, stderr); }

int main(int argc, char **argv)
{
    if (cervus_check_help_version(argc, argv, USAGE, "cp")) return 0;

    cp_opts_t o;
    memset(&o, 0, sizeof(o));

    int opt;
    while ((opt = getopt(argc, argv, "fipRrv")) != -1) {
        switch (opt) {
            case 'f': o.force = 1; break;
            case 'i': o.interactive = 1; break;
            case 'p': o.preserve = 1; break;
            case 'R': case 'r': o.recursive = 1; break;
            case 'v': o.verbose = 1; break;
            default: usage(); return 1;
        }
    }

    int nf = argc - optind;
    if (nf < 2) { usage(); return 1; }

    const char *target = argv[argc - 1];
    char dst_resolved[512];
    snprintf(dst_resolved, sizeof(dst_resolved), "%s", target);

    struct stat dst_st;
    int dst_is_dir = (stat(dst_resolved, &dst_st) == 0 && dst_st.st_type == 1);

    if (nf > 2 && !dst_is_dir) {
        fprintf(stderr, "cp: target '%s' is not a directory\n", target);
        return 1;
    }

    int rc = 0;
    for (int i = optind; i < argc - 1; i++) {
        char src_resolved[512];
        snprintf(src_resolved, sizeof(src_resolved), "%s", argv[i]);

        struct stat src_st;
        if (stat(src_resolved, &src_st) != 0) {
            fprintf(stderr, "cp: '%s': no such file\n", argv[i]);
            rc = 1; continue;
        }
        if (src_st.st_type == 1 && !o.recursive) {
            fprintf(stderr, "cp: '%s': is a directory (use -R)\n", argv[i]);
            rc = 1; continue;
        }

        char final_dst[512];
        if (dst_is_dir) {
            snprintf(final_dst, sizeof(final_dst), "%s/%s",
                     dst_resolved, path_basename(argv[i]));
        } else {
            strncpy(final_dst, dst_resolved, sizeof(final_dst) - 1);
            final_dst[sizeof(final_dst) - 1] = '\0';
        }

        if (src_st.st_type == 1) {
            if (copy_tree(src_resolved, final_dst, &o) != 0) rc = 1;
        } else {
            if (copy_file(src_resolved, final_dst, &o) != 0) rc = 1;
        }
    }
    return rc;
}
