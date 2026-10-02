#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <cervus_util.h>

static int copy_tree(const char *src, const char *dst);

static int copy_file(const char *src, const char *dst, const struct stat *st)
{
    int in = open(src, O_RDONLY);
    if (in < 0) return -1;
    int out = open(dst, O_WRONLY | O_CREAT | O_TRUNC, st->st_mode & 07777);
    if (out < 0) { close(in); return -1; }
    static char buf[65536];
    ssize_t n;
    int rc = 0;
    while ((n = read(in, buf, sizeof buf)) > 0) {
        ssize_t off = 0;
        while (off < n) {
            ssize_t w = write(out, buf + off, (size_t)(n - off));
            if (w <= 0) { rc = -1; break; }
            off += w;
        }
        if (rc) break;
    }
    if (n < 0) rc = -1;
    close(in);
    if (close(out) < 0) rc = -1;
    if (rc == 0) {
        chmod(dst, st->st_mode & 07777);
        if (getuid() == 0) chown(dst, st->st_uid, st->st_gid);
        struct timespec ts[2] = { st->st_atim, st->st_mtim };
        utimensat(AT_FDCWD, dst, ts, 0);
    }
    return rc;
}

static int copy_tree(const char *src, const char *dst)
{
    struct stat st;
    if (lstat(src, &st) < 0) return -1;
    if (S_ISLNK(st.st_mode)) {
        char target[1024];
        ssize_t n = readlink(src, target, sizeof target - 1);
        if (n < 0) return -1;
        target[n] = 0;
        return symlink(target, dst);
    }
    if (S_ISDIR(st.st_mode)) {
        if (mkdir(dst, st.st_mode & 07777) < 0 && errno != EEXIST) return -1;
        DIR *d = opendir(src);
        if (!d) return -1;
        struct dirent *e;
        int rc = 0;
        while ((e = readdir(d))) {
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            char s2[1024], d2[1024];
            snprintf(s2, sizeof s2, "%s/%s", src, e->d_name);
            snprintf(d2, sizeof d2, "%s/%s", dst, e->d_name);
            if (copy_tree(s2, d2) < 0) { rc = -1; break; }
        }
        closedir(d);
        chmod(dst, st.st_mode & 07777);
        return rc;
    }
    return copy_file(src, dst, &st);
}

static int remove_tree(const char *path)
{
    struct stat st;
    if (lstat(path, &st) < 0) return -1;
    if (!S_ISDIR(st.st_mode)) return unlink(path);
    DIR *d = opendir(path);
    if (!d) return -1;
    struct dirent *e;
    int rc = 0;
    while ((e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        char p2[1024];
        snprintf(p2, sizeof p2, "%s/%s", path, e->d_name);
        if (remove_tree(p2) < 0) rc = -1;
    }
    closedir(d);
    if (rmdir(path) < 0) rc = -1;
    return rc;
}

static int move_across(const char *src, const char *dst)
{
    struct stat ds;
    if (lstat(dst, &ds) == 0) {
        if (S_ISDIR(ds.st_mode)) { if (rmdir(dst) < 0) return -1; }
        else if (unlink(dst) < 0) return -1;
    }
    if (copy_tree(src, dst) < 0) {
        int e = errno;
        remove_tree(dst);
        errno = e;
        return -1;
    }
    return remove_tree(src);
}

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
    argc = cervus_end_of_options(argc, argv);

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
    snprintf(dst_resolved, sizeof(dst_resolved), "%s", target);

    struct stat dst_st;
    int dst_is_dir = (stat(dst_resolved, &dst_st) == 0 && dst_st.st_type == 1);
    if (nf > 2 && !dst_is_dir) {
        fprintf(stderr, "mv: target '%s' is not a directory\n", target);
        return 1;
    }

    int rc = 0;
    for (int i = optind; i < argc - 1; i++) {
        char sp[512], dp[512];
        snprintf(sp, sizeof(sp), "%s", argv[i]);
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
        }

        if (rename(sp, dp) < 0) {
            if (errno != EXDEV || move_across(sp, dp) < 0) {
                fprintf(stderr, "mv: cannot move '%s' to '%s': %s\n", argv[i], dp, strerror(errno));
                rc = 1; continue;
            }
        }
        if (verbose) fprintf(stderr, "'%s' -> '%s'\n", argv[i], dp);
    }
    return rc;
}
