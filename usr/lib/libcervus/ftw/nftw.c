#include <ftw.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <libcervus.h>

static int walk(char *path, size_t len, size_t cap, int level,
                int (*fn)(const char *, const struct stat *, int, struct FTW *),
                int flags, dev_t root_dev)
{
    struct stat st;
    int type;
    int sr = (flags & FTW_PHYS) ? lstat(path, &st) : stat(path, &st);
    if (sr < 0) {
        if (!(flags & FTW_PHYS) && lstat(path, &st) == 0 && S_ISLNK(st.st_mode)) type = FTW_SLN;
        else if (errno == EACCES || errno == ENOENT) { type = FTW_NS; memset(&st, 0, sizeof st); }
        else return -1;
    } else if (S_ISDIR(st.st_mode)) {
        type = (flags & FTW_DEPTH) ? FTW_DP : FTW_D;
    } else if (S_ISLNK(st.st_mode)) {
        type = FTW_SL;
    } else {
        type = FTW_F;
    }
    if (level > 0 && (flags & FTW_MOUNT) && type != FTW_NS && st.st_dev != root_dev) return 0;

    struct FTW ftw;
    ftw.level = level;
    size_t base = len;
    while (base > 0 && path[base - 1] != '/') base--;
    if (base == len && len > 0) base = 0;
    ftw.base = (int)base;

    if (type != FTW_D && type != FTW_DP) return fn(path, &st, type, &ftw);

    DIR *d = opendir(path);
    if (!d) {
        if (errno != EACCES) return -1;
        return fn(path, &st, FTW_DNR, &ftw);
    }
    int r = 0;
    if (type == FTW_D) {
        r = fn(path, &st, FTW_D, &ftw);
        if (r) { closedir(d); return r; }
    }
    if ((flags & FTW_CHDIR) && chdir(path) < 0) { closedir(d); return -1; }
    struct dirent *de;
    while ((de = readdir(d))) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
        size_t nl = strlen(de->d_name);
        size_t sep = len && path[len - 1] != '/' ? 1 : 0;
        if (len + sep + nl + 1 > cap) { closedir(d); __cervus_set_errno(ENAMETOOLONG); return -1; }
        if (sep) path[len] = '/';
        memcpy(path + len + sep, de->d_name, nl + 1);
        r = walk(path, len + sep + nl, cap, level + 1, fn, flags, root_dev);
        path[len] = '\0';
        if (r) { closedir(d); return r; }
    }
    closedir(d);
    if (flags & FTW_CHDIR) {
        char saved = path[base > 1 ? base - 1 : base];
        size_t cut = base > 1 ? base - 1 : base;
        path[cut] = '\0';
        int cr = chdir(base ? path : ".");
        path[cut] = saved;
        if (base == 0) cr = 0;
        if (cr < 0) return -1;
    }
    if (type == FTW_DP) r = fn(path, &st, FTW_DP, &ftw);
    return r;
}

int nftw(const char *dir, int (*fn)(const char *, const struct stat *, int, struct FTW *), int fd_limit, int flags)
{
    (void)fd_limit;
    if (!dir || !*dir) { __cervus_set_errno(ENOENT); return -1; }
    size_t cap = PATH_MAX;
    char *path = malloc(cap);
    if (!path) return -1;
    size_t len = strlen(dir);
    int cwdfd = -1;
    size_t off = 0;
    if (flags & FTW_CHDIR) {
        cwdfd = open(".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (cwdfd < 0) { free(path); return -1; }
        if (dir[0] != '/') {
            if (!getcwd(path, cap)) { close(cwdfd); free(path); return -1; }
            off = strlen(path);
            if (off > 1) path[off++] = '/';
        }
    }
    if (off + len + 1 > cap) { if (cwdfd >= 0) close(cwdfd); free(path); __cervus_set_errno(ENAMETOOLONG); return -1; }
    memcpy(path + off, dir, len + 1);
    struct stat rst;
    dev_t root_dev = stat(path, &rst) == 0 ? rst.st_dev : 0;
    int r = walk(path, off + len, cap, 0, fn, flags, root_dev);
    if (cwdfd >= 0) {
        int e = errno;
        fchdir(cwdfd);
        close(cwdfd);
        __cervus_set_errno(e);
    }
    free(path);
    return r;
}

static int (*g_ftw_fn)(const char *, const struct stat *, int);

static int ftw_adapter(const char *p, const struct stat *st, int type, struct FTW *f)
{
    (void)f;
    if (type == FTW_DP) type = FTW_D;
    if (type == FTW_SLN) type = FTW_NS;
    return g_ftw_fn(p, st, type);
}

int ftw(const char *dir, int (*fn)(const char *, const struct stat *, int), int fd_limit)
{
    g_ftw_fn = fn;
    return nftw(dir, ftw_adapter, fd_limit, 0);
}
