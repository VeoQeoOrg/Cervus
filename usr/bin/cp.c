#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <getopt.h>
#include <limits.h>
#include <sys/stat.h>
#include <cervus_util.h>

static const char USAGE[] =
    "Usage: cp [OPTION]... SOURCE DEST\n"
    "  or:  cp [OPTION]... SOURCE... DIRECTORY\n"
    "  or:  cp [OPTION]... -t DIRECTORY SOURCE...\n"
    "Copy SOURCE to DEST, or several SOURCEs into DIRECTORY.\n\n"
    "  -a, --archive            same as -dR --preserve=all\n"
    "  -d                       same as -P --preserve=links\n"
    "  -f, --force              remove a destination that cannot be opened and retry\n"
    "  -i, --interactive        ask before overwriting\n"
    "  -n, --no-clobber         never overwrite an existing file\n"
    "  -H                       follow symlinks given on the command line\n"
    "  -L, --dereference        always follow symlinks in SOURCE\n"
    "  -P, --no-dereference     never follow symlinks in SOURCE (default with -R)\n"
    "  -l, --link               hard-link files instead of copying\n"
    "  -s, --symbolic-link      make symbolic links instead of copying\n"
    "  -p                       preserve mode, ownership and timestamps\n"
    "      --preserve[=LIST]    preserve mode,ownership,timestamps (default: all three)\n"
    "  -R, -r, --recursive      copy directories recursively\n"
    "  -t, --target-directory=DIR  copy all SOURCE arguments into DIR\n"
    "  -T, --no-target-directory   treat DEST as a normal file\n"
    "  -u, --update             copy only when SOURCE is newer than DEST or DEST is missing\n"
    "  -v, --verbose            explain what is being done\n";

static int o_recursive, o_force, o_interactive, o_noclobber, o_link, o_symlink;
static int o_update, o_verbose, o_keep_mode, o_keep_owner, o_keep_times;
static int o_deref = -1;
static mode_t g_umask;
static int g_rc;

static void complain(const char *what, const char *path)
{
    fprintf(stderr, "cp: %s '%s': %s\n", what, path, strerror(errno));
    g_rc = 1;
}

static int same_file(const struct stat *a, const struct stat *b)
{
    return a->st_dev == b->st_dev && a->st_ino == b->st_ino;
}

static void apply_attrs(const char *dst, const struct stat *st, int is_link)
{
    if (o_keep_owner) {
        if ((is_link ? lchown(dst, st->st_uid, st->st_gid) : chown(dst, st->st_uid, st->st_gid)) < 0 &&
            errno != EPERM)
            complain("cannot preserve ownership of", dst);
    }
    if (o_keep_mode && !is_link && chmod(dst, st->st_mode & 07777) < 0)
        complain("cannot preserve permissions of", dst);
    if (o_keep_times) {
        struct timespec ts[2] = { st->st_atim, st->st_mtim };
        if (utimensat(AT_FDCWD, dst, ts, is_link ? AT_SYMLINK_NOFOLLOW : 0) < 0 && !is_link)
            complain("cannot preserve times of", dst);
    }
}

static int ask_overwrite(const char *dst)
{
    fprintf(stderr, "cp: overwrite '%s'? ", dst);
    char ans[16];
    ssize_t n = read(0, ans, sizeof ans - 1);
    return n > 0 && (ans[0] == 'y' || ans[0] == 'Y');
}

static int copy_data(const char *src, const char *dst, const struct stat *st, int dst_exists)
{
    int sfd = open(src, O_RDONLY);
    if (sfd < 0) { complain("cannot open", src); return -1; }
    mode_t mode = o_keep_mode ? (st->st_mode & 07777) : (st->st_mode & 0777);
    int dfd = open(dst, O_WRONLY | O_CREAT | O_TRUNC, mode);
    if (dfd < 0 && dst_exists && o_force) {
        unlink(dst);
        dfd = open(dst, O_WRONLY | O_CREAT | O_TRUNC, mode);
    }
    if (dfd < 0) { complain("cannot create regular file", dst); close(sfd); return -1; }
    static char buf[128 * 1024];
    ssize_t n;
    int rc = 0;
    while ((n = read(sfd, buf, sizeof buf)) > 0) {
        for (ssize_t off = 0; off < n;) {
            ssize_t w = write(dfd, buf + off, (size_t)(n - off));
            if (w < 0) {
                if (errno == EINTR) continue;
                complain("error writing", dst);
                rc = -1;
                break;
            }
            off += w;
        }
        if (rc) break;
    }
    if (n < 0) { complain("error reading", src); rc = -1; }
    close(sfd);
    if (close(dfd) < 0 && !rc) { complain("error writing", dst); rc = -1; }
    return rc;
}

static int copy_entry(const char *src, const char *dst, int top);

static int copy_dir(const char *src, const char *dst, const struct stat *st)
{
    struct stat dst_st;
    int created = 0;
    if (stat(dst, &dst_st) == 0) {
        if (!S_ISDIR(dst_st.st_mode)) {
            fprintf(stderr, "cp: cannot overwrite non-directory '%s' with directory '%s'\n", dst, src);
            g_rc = 1;
            return -1;
        }
        if (same_file(st, &dst_st)) {
            fprintf(stderr, "cp: cannot copy a directory, '%s', into itself\n", src);
            g_rc = 1;
            return -1;
        }
    } else {
        if (mkdir(dst, (st->st_mode & 0777) | S_IRWXU) < 0) { complain("cannot create directory", dst); return -1; }
        created = 1;
        if (o_verbose) printf("'%s' -> '%s'\n", src, dst);
    }
    DIR *d = opendir(src);
    if (!d) { complain("cannot open directory", src); return -1; }
    size_t sl = strlen(src), dl = strlen(dst);
    struct dirent *de;
    while ((de = readdir(d))) {
        const char *nm = de->d_name;
        if (nm[0] == '.' && (!nm[1] || (nm[1] == '.' && !nm[2]))) continue;
        size_t nl = strlen(nm);
        char *sp = malloc(sl + nl + 2), *dp = malloc(dl + nl + 2);
        if (!sp || !dp) { fprintf(stderr, "cp: out of memory\n"); exit(1); }
        snprintf(sp, sl + nl + 2, "%s%s%s", src, sl && src[sl - 1] == '/' ? "" : "/", nm);
        snprintf(dp, dl + nl + 2, "%s%s%s", dst, dl && dst[dl - 1] == '/' ? "" : "/", nm);
        struct stat child;
        if (lstat(sp, &child) == 0 && S_ISDIR(child.st_mode) && stat(dst, &dst_st) == 0 && same_file(&child, &dst_st)) {
            fprintf(stderr, "cp: cannot copy a directory, '%s', into itself, '%s'\n", src, dst);
            g_rc = 1;
        } else {
            copy_entry(sp, dp, 0);
        }
        free(sp);
        free(dp);
    }
    closedir(d);
    if (created || o_keep_mode) {
        mode_t m = o_keep_mode ? (st->st_mode & 07777) : ((st->st_mode & 0777) & ~g_umask);
        if (chmod(dst, m) < 0) complain("cannot set permissions of", dst);
    }
    if (o_keep_owner || o_keep_times) {
        int km = o_keep_mode;
        o_keep_mode = 0;
        apply_attrs(dst, st, 0);
        o_keep_mode = km;
    }
    return 0;
}

static int copy_entry(const char *src, const char *dst, int top)
{
    struct stat st;
    int follow = o_deref == 'L' || (o_deref == 'H' && top);
    if ((follow ? stat(src, &st) : lstat(src, &st)) < 0) { complain("cannot stat", src); return -1; }

    if (S_ISDIR(st.st_mode)) {
        if (!o_recursive) {
            fprintf(stderr, "cp: -r not specified; omitting directory '%s'\n", src);
            g_rc = 1;
            return -1;
        }
        return copy_dir(src, dst, &st);
    }

    struct stat dst_st;
    int dst_exists = lstat(dst, &dst_st) == 0;
    if (dst_exists) {
        struct stat dfollow;
        if (stat(dst, &dfollow) == 0 && same_file(&st, &dfollow) && !S_ISLNK(st.st_mode)) {
            fprintf(stderr, "cp: '%s' and '%s' are the same file\n", src, dst);
            g_rc = 1;
            return -1;
        }
        if (S_ISDIR(dst_st.st_mode)) {
            fprintf(stderr, "cp: cannot overwrite directory '%s' with non-directory\n", dst);
            g_rc = 1;
            return -1;
        }
        if (o_noclobber) return 0;
        if (o_update && (dst_st.st_mtim.tv_sec > st.st_mtim.tv_sec ||
                         (dst_st.st_mtim.tv_sec == st.st_mtim.tv_sec && dst_st.st_mtim.tv_nsec >= st.st_mtim.tv_nsec)))
            return 0;
        if (o_interactive && !ask_overwrite(dst)) return 0;
    }

    int rc;
    if (o_link) {
        if (dst_exists) unlink(dst);
        rc = link(src, dst);
        if (rc < 0) complain("cannot create hard link", dst);
    } else if (o_symlink) {
        if (dst_exists) unlink(dst);
        rc = symlink(src, dst);
        if (rc < 0) complain("cannot create symbolic link", dst);
    } else if (S_ISLNK(st.st_mode)) {
        char target[PATH_MAX];
        ssize_t n = readlink(src, target, sizeof target - 1);
        if (n < 0) { complain("cannot read symbolic link", src); return -1; }
        target[n] = 0;
        if (dst_exists) unlink(dst);
        rc = symlink(target, dst);
        if (rc < 0) complain("cannot create symbolic link", dst);
        else apply_attrs(dst, &st, 1);
    } else if (o_recursive && (S_ISFIFO(st.st_mode) || S_ISCHR(st.st_mode) || S_ISBLK(st.st_mode) || S_ISSOCK(st.st_mode))) {
        if (dst_exists) unlink(dst);
        rc = mknod(dst, st.st_mode, st.st_rdev);
        if (rc < 0) complain("cannot create special file", dst);
        else apply_attrs(dst, &st, 0);
    } else {
        rc = copy_data(src, dst, &st, dst_exists);
        if (rc == 0) apply_attrs(dst, &st, 0);
    }
    if (rc == 0 && o_verbose) printf("'%s' -> '%s'\n", src, dst);
    return rc;
}

static const char *base_name(const char *p)
{
    size_t l = strlen(p);
    while (l > 1 && p[l - 1] == '/') l--;
    const char *s = p + l;
    while (s > p && s[-1] != '/') s--;
    return s;
}

static void set_preserve(const char *list)
{
    if (!list || !*list || !strcmp(list, "all")) { o_keep_mode = o_keep_owner = o_keep_times = 1; return; }
    char buf[128];
    snprintf(buf, sizeof buf, "%s", list);
    for (char *t = strtok(buf, ","); t; t = strtok(NULL, ",")) {
        if (!strcmp(t, "mode")) o_keep_mode = 1;
        else if (!strcmp(t, "ownership")) o_keep_owner = 1;
        else if (!strcmp(t, "timestamps")) o_keep_times = 1;
        else if (!strcmp(t, "links") || !strcmp(t, "xattr") || !strcmp(t, "context")) {}
        else { fprintf(stderr, "cp: invalid attribute '%s'\n", t); exit(1); }
    }
}

int main(int argc, char **argv)
{
    if (cervus_check_help_version(argc, argv, USAGE, "cp")) return 0;
    static const struct option longopts[] = {
        { "archive",             no_argument,       0, 'a' },
        { "force",               no_argument,       0, 'f' },
        { "interactive",         no_argument,       0, 'i' },
        { "no-clobber",          no_argument,       0, 'n' },
        { "dereference",         no_argument,       0, 'L' },
        { "no-dereference",      no_argument,       0, 'P' },
        { "link",                no_argument,       0, 'l' },
        { "symbolic-link",       no_argument,       0, 's' },
        { "recursive",           no_argument,       0, 'R' },
        { "target-directory",    required_argument, 0, 't' },
        { "no-target-directory", no_argument,       0, 'T' },
        { "update",              no_argument,       0, 'u' },
        { "verbose",             no_argument,       0, 'v' },
        { "preserve",            optional_argument, 0, 'E' },
        { 0, 0, 0, 0 }
    };
    const char *target_dir = NULL;
    int no_target = 0, opt;
    while ((opt = getopt_long(argc, argv, "adfinHLPlspRrt:Tuv", longopts, NULL)) != -1) {
        switch (opt) {
        case 'a': o_recursive = 1; o_deref = 'P'; set_preserve(NULL); break;
        case 'd': o_deref = 'P'; break;
        case 'f': o_force = 1; o_interactive = 0; o_noclobber = 0; break;
        case 'i': o_interactive = 1; o_noclobber = 0; break;
        case 'n': o_noclobber = 1; o_interactive = 0; break;
        case 'H': o_deref = 'H'; break;
        case 'L': o_deref = 'L'; break;
        case 'P': o_deref = 'P'; break;
        case 'l': o_link = 1; break;
        case 's': o_symlink = 1; break;
        case 'p': set_preserve(NULL); break;
        case 'E': set_preserve(optarg); break;
        case 'R': case 'r': o_recursive = 1; break;
        case 't': target_dir = optarg; break;
        case 'T': no_target = 1; break;
        case 'u': o_update = 1; break;
        case 'v': o_verbose = 1; break;
        default: fputs(USAGE, stderr); return 1;
        }
    }
    if (o_deref < 0) o_deref = o_recursive ? 'P' : 'L';
    g_umask = umask(0);
    umask(g_umask);

    int n = argc - optind;
    char **args = argv + optind;
    if (target_dir) {
        if (n < 1) { fputs("cp: missing file operand\n", stderr); return 1; }
    } else {
        if (n < 2) { fputs(n ? "cp: missing destination file operand\n" : "cp: missing file operand\n", stderr); return 1; }
        const char *last = args[n - 1];
        struct stat st;
        int isdir = !no_target && stat(last, &st) == 0 && S_ISDIR(st.st_mode);
        if (n == 2 && !isdir) {
            copy_entry(args[0], last, 1);
            return g_rc;
        }
        if (no_target) { fprintf(stderr, "cp: extra operand '%s'\n", args[2]); return 1; }
        if (!isdir) { fprintf(stderr, "cp: target '%s' is not a directory\n", last); return 1; }
        target_dir = last;
        n--;
    }
    size_t tl = strlen(target_dir);
    for (int i = 0; i < n; i++) {
        const char *b = base_name(args[i]);
        size_t need = tl + strlen(b) + 2;
        char *dst = malloc(need);
        if (!dst) { fputs("cp: out of memory\n", stderr); return 1; }
        snprintf(dst, need, "%s%s%s", target_dir, tl && target_dir[tl - 1] == '/' ? "" : "/", b);
        copy_entry(args[i], dst, 1);
        free(dst);
    }
    return g_rc;
}
