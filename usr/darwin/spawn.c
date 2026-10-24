#include <spawn.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <grp.h>
#include <mntent.h>
#include <limits.h>
#include <unistd.h>
#include <sys/statfs.h>
#include <sys/statvfs.h>
#include "darwin.h"

static int rc(int e)
{
    return e ? d_errno_lx2d(e) : 0;
}

static int d_fa_init(posix_spawn_file_actions_t **fa)
{
    *fa = calloc(1, sizeof **fa);
    if (!*fa)
        return 12;
    return rc(posix_spawn_file_actions_init(*fa));
}

static int d_fa_destroy(posix_spawn_file_actions_t **fa)
{
    if (!*fa)
        return 22;
    posix_spawn_file_actions_destroy(*fa);
    free(*fa);
    *fa = NULL;
    return 0;
}

static int d_fa_addopen(posix_spawn_file_actions_t **fa, int fd, const char *p, int fl, mode_t m)
{
    return rc(posix_spawn_file_actions_addopen(*fa, fd, p, d_open_flags(fl), m));
}

static int d_fa_addclose(posix_spawn_file_actions_t **fa, int fd) { return rc(posix_spawn_file_actions_addclose(*fa, fd)); }
static int d_fa_adddup2(posix_spawn_file_actions_t **fa, int fd, int nfd) { return rc(posix_spawn_file_actions_adddup2(*fa, fd, nfd)); }
static int d_fa_addchdir(posix_spawn_file_actions_t **fa, const char *p) { return rc(posix_spawn_file_actions_addchdir_np(*fa, p)); }
static int d_fa_addfchdir(posix_spawn_file_actions_t **fa, int fd) { return rc(posix_spawn_file_actions_addfchdir_np(*fa, fd)); }
static int d_fa_addinherit(posix_spawn_file_actions_t **fa, int fd) { (void)fa; (void)fd; return 0; }

DSYM("_posix_spawn_file_actions_init", d_fa_init);
DSYM("_posix_spawn_file_actions_destroy", d_fa_destroy);
DSYM("_posix_spawn_file_actions_addopen", d_fa_addopen);
DSYM("_posix_spawn_file_actions_addclose", d_fa_addclose);
DSYM("_posix_spawn_file_actions_adddup2", d_fa_adddup2);
DSYM("_posix_spawn_file_actions_addchdir_np", d_fa_addchdir);
DSYM("_posix_spawn_file_actions_addfchdir_np", d_fa_addfchdir);
DSYM("_posix_spawn_file_actions_addinherit_np", d_fa_addinherit);

static void sigset_d2lx(uint32_t d, sigset_t *out)
{
    sigemptyset(out);
    for (int i = 1; i < 32; i++)
        if ((d & (1u << (i - 1))) && d_sig_d2lx(i))
            sigaddset(out, d_sig_d2lx(i));
}

static uint32_t sigset_lx2d(const sigset_t *s)
{
    uint32_t d = 0;
    for (int i = 1; i < 32; i++)
        if (d_sig_d2lx(i) && sigismember(s, d_sig_d2lx(i)) == 1)
            d |= 1u << (i - 1);
    return d;
}

static int d_sa_init(posix_spawnattr_t **a)
{
    *a = calloc(1, sizeof **a);
    if (!*a)
        return 12;
    return rc(posix_spawnattr_init(*a));
}

static int d_sa_destroy(posix_spawnattr_t **a)
{
    if (!*a)
        return 22;
    posix_spawnattr_destroy(*a);
    free(*a);
    *a = NULL;
    return 0;
}

static int d_sa_setflags(posix_spawnattr_t **a, short f)
{
    short l = f & (POSIX_SPAWN_RESETIDS | POSIX_SPAWN_SETPGROUP | POSIX_SPAWN_SETSIGDEF | POSIX_SPAWN_SETSIGMASK);
    if (f & 0x400)
        l |= POSIX_SPAWN_SETSID;
    return rc(posix_spawnattr_setflags(*a, l));
}

static int d_sa_getflags(posix_spawnattr_t **a, short *f)
{
    short l;
    int e = posix_spawnattr_getflags(*a, &l);
    *f = (short)((l & 0x0f) | ((l & POSIX_SPAWN_SETSID) ? 0x400 : 0));
    return rc(e);
}

static int d_sa_setpgroup(posix_spawnattr_t **a, pid_t p) { return rc(posix_spawnattr_setpgroup(*a, p)); }
static int d_sa_getpgroup(posix_spawnattr_t **a, pid_t *p) { return rc(posix_spawnattr_getpgroup(*a, p)); }

static int d_sa_setsigdefault(posix_spawnattr_t **a, const uint32_t *s)
{
    sigset_t l;
    sigset_d2lx(*s, &l);
    return rc(posix_spawnattr_setsigdefault(*a, &l));
}

static int d_sa_getsigdefault(posix_spawnattr_t **a, uint32_t *s)
{
    sigset_t l;
    int e = posix_spawnattr_getsigdefault(*a, &l);
    *s = sigset_lx2d(&l);
    return rc(e);
}

static int d_sa_setsigmask(posix_spawnattr_t **a, const uint32_t *s)
{
    sigset_t l;
    sigset_d2lx(*s, &l);
    return rc(posix_spawnattr_setsigmask(*a, &l));
}

static int d_sa_getsigmask(posix_spawnattr_t **a, uint32_t *s)
{
    sigset_t l;
    int e = posix_spawnattr_getsigmask(*a, &l);
    *s = sigset_lx2d(&l);
    return rc(e);
}

static int d_sa_noop(posix_spawnattr_t **a, long v) { (void)a; (void)v; return 0; }

DSYM("_posix_spawnattr_init", d_sa_init);
DSYM("_posix_spawnattr_destroy", d_sa_destroy);
DSYM("_posix_spawnattr_setflags", d_sa_setflags);
DSYM("_posix_spawnattr_getflags", d_sa_getflags);
DSYM("_posix_spawnattr_setpgroup", d_sa_setpgroup);
DSYM("_posix_spawnattr_getpgroup", d_sa_getpgroup);
DSYM("_posix_spawnattr_setsigdefault", d_sa_setsigdefault);
DSYM("_posix_spawnattr_getsigdefault", d_sa_getsigdefault);
DSYM("_posix_spawnattr_setsigmask", d_sa_setsigmask);
DSYM("_posix_spawnattr_getsigmask", d_sa_getsigmask);
DSYM("_posix_spawnattr_setbinpref_np", d_sa_noop);
DSYM("_posix_spawnattr_set_qos_class_np", d_sa_noop);

static int d_posix_spawn(pid_t *pid, const char *path, posix_spawn_file_actions_t **fa, posix_spawnattr_t **a,
                         char *const argv[], char *const envp[])
{
    return rc(posix_spawn(pid, path, fa ? *fa : NULL, a ? *a : NULL, argv, envp));
}

static int d_posix_spawnp(pid_t *pid, const char *file, posix_spawn_file_actions_t **fa, posix_spawnattr_t **a,
                          char *const argv[], char *const envp[])
{
    return rc(posix_spawnp(pid, file, fa ? *fa : NULL, a ? *a : NULL, argv, envp));
}

DSYM("_posix_spawn", d_posix_spawn);
DSYM("_posix_spawnp", d_posix_spawnp);

static int d_getgrnam_r(const char *n, struct group *g, char *b, size_t l, struct group **r) { return rc(getgrnam_r(n, g, b, l, r)); }
static int d_getgrgid_r(gid_t id, struct group *g, char *b, size_t l, struct group **r) { return rc(getgrgid_r(id, g, b, l, r)); }
DSYM("_getgrnam_r", d_getgrnam_r);
DSYM("_getgrgid_r", d_getgrgid_r);

struct dstatfs {
    uint32_t f_bsize;
    int32_t f_iosize;
    uint64_t f_blocks, f_bfree, f_bavail, f_files, f_ffree;
    int32_t f_fsid[2];
    uint32_t f_owner, f_type, f_flags, f_fssubtype;
    char f_fstypename[16];
    char f_mntonname[1024];
    char f_mntfromname[1024];
    uint32_t f_flags_ext;
    uint32_t f_reserved[7];
};
_Static_assert(sizeof(struct dstatfs) == 2168, "Darwin statfs64 is 2168 bytes");

struct mnt {
    char dir[PATH_MAX], from[PATH_MAX], type[16];
    int ro;
};

static int find_mount(const char *real, struct mnt *out)
{
    FILE *f = setmntent("/proc/mounts", "r");
    struct mntent *e;
    size_t best = 0;
    int found = 0;
    if (!f)
        return 0;
    while ((e = getmntent(f))) {
        size_t n = strlen(e->mnt_dir);
        int match = !strcmp(e->mnt_dir, "/") ||
                    (!strncmp(real, e->mnt_dir, n) && (real[n] == '/' || real[n] == 0));
        if (match && (n > best || !found)) {
            best = n;
            found = 1;
            snprintf(out->dir, sizeof out->dir, "%s", e->mnt_dir);
            snprintf(out->from, sizeof out->from, "%s", e->mnt_fsname);
            snprintf(out->type, sizeof out->type, "%s", e->mnt_type);
            out->ro = hasmntopt(e, "ro") != NULL;
        }
    }
    endmntent(f);
    return found;
}

static void fill_dstatfs(const struct statfs *s, const struct mnt *m, struct dstatfs *d)
{
    memset(d, 0, sizeof *d);
    d->f_bsize = (uint32_t)(s->f_frsize ? s->f_frsize : s->f_bsize);
    d->f_iosize = (int32_t)s->f_bsize;
    d->f_blocks = s->f_blocks;
    d->f_bfree = s->f_bfree;
    d->f_bavail = s->f_bavail;
    d->f_files = s->f_files;
    d->f_ffree = s->f_ffree;
    memcpy(d->f_fsid, &s->f_fsid, sizeof d->f_fsid);
    d->f_type = (uint32_t)s->f_type;
    if (m) {
        d->f_flags = (m->ro ? 1u : 0u) | 0x1000u;
        snprintf(d->f_fstypename, sizeof d->f_fstypename, "%s", m->type);
        snprintf(d->f_mntonname, sizeof d->f_mntonname, "%s", m->dir);
        snprintf(d->f_mntfromname, sizeof d->f_mntfromname, "%s", m->from);
    }
}

static int d_statfs(const char *p, struct dstatfs *d)
{
    struct statfs s;
    struct mnt m;
    char real[PATH_MAX];
    if (statfs(p, &s) < 0)
        return -1;
    int have = realpath(p, real) && find_mount(real, &m);
    fill_dstatfs(&s, have ? &m : NULL, d);
    return 0;
}

static int d_fstatfs(int fd, struct dstatfs *d)
{
    struct statfs s;
    struct mnt m;
    char link[64], real[PATH_MAX];
    if (fstatfs(fd, &s) < 0)
        return -1;
    snprintf(link, sizeof link, "/proc/self/fd/%d", fd);
    ssize_t n = readlink(link, real, sizeof real - 1);
    int have = 0;
    if (n > 0) {
        real[n] = 0;
        have = find_mount(real, &m);
    }
    fill_dstatfs(&s, have ? &m : NULL, d);
    return 0;
}

static int d_getfsstat(struct dstatfs *buf, int size, int flags)
{
    (void)flags;
    FILE *f = setmntent("/proc/mounts", "r");
    struct mntent *e;
    int n = 0, cap = buf ? size / (int)sizeof *buf : 0;
    if (!f)
        return -1;
    while ((e = getmntent(f))) {
        if (buf && n < cap) {
            struct statfs s;
            struct mnt m;
            snprintf(m.dir, sizeof m.dir, "%s", e->mnt_dir);
            snprintf(m.from, sizeof m.from, "%s", e->mnt_fsname);
            snprintf(m.type, sizeof m.type, "%s", e->mnt_type);
            m.ro = hasmntopt(e, "ro") != NULL;
            if (statfs(e->mnt_dir, &s) < 0)
                memset(&s, 0, sizeof s);
            fill_dstatfs(&s, &m, &buf[n]);
        }
        n++;
    }
    endmntent(f);
    return buf && n > cap ? cap : n;
}

static int d_getmntinfo(struct dstatfs **out, int flags)
{
    static struct dstatfs *list;
    int n = d_getfsstat(NULL, 0, flags);
    if (n <= 0)
        return n;
    struct dstatfs *nl = realloc(list, (size_t)n * sizeof *nl);
    if (!nl)
        return 0;
    list = nl;
    n = d_getfsstat(list, n * (int)sizeof *list, flags);
    *out = list;
    return n;
}

DSYM("_statfs$INODE64", d_statfs);
DSYM("_fstatfs$INODE64", d_fstatfs);
DSYM("_statfs64", d_statfs);
DSYM("_fstatfs64", d_fstatfs);
DSYM("_getfsstat$INODE64", d_getfsstat);
DSYM("_getfsstat64", d_getfsstat);
DSYM("_getmntinfo$INODE64", d_getmntinfo);
DSYM("_getmntinfo64", d_getmntinfo);

struct dstatvfs {
    unsigned long f_bsize, f_frsize;
    uint32_t f_blocks, f_bfree, f_bavail, f_files, f_ffree, f_favail;
    unsigned long f_fsid, f_flag, f_namemax;
};

static uint32_t clamp32(uint64_t v) { return v > 0xffffffffu ? 0xffffffffu : (uint32_t)v; }

static void conv_vfs(const struct statvfs *s, struct dstatvfs *d)
{
    uint64_t scale = 1;
    uint64_t blocks = s->f_blocks;
    while (blocks / scale > 0xffffffffull)
        scale *= 2;
    d->f_bsize = s->f_bsize;
    d->f_frsize = s->f_frsize * scale;
    d->f_blocks = clamp32(s->f_blocks / scale);
    d->f_bfree = clamp32(s->f_bfree / scale);
    d->f_bavail = clamp32(s->f_bavail / scale);
    d->f_files = clamp32(s->f_files);
    d->f_ffree = clamp32(s->f_ffree);
    d->f_favail = clamp32(s->f_favail);
    d->f_fsid = s->f_fsid;
    d->f_flag = s->f_flag & 3;
    d->f_namemax = s->f_namemax;
}

static int d_statvfs(const char *p, struct dstatvfs *d)
{
    struct statvfs s;
    if (statvfs(p, &s) < 0)
        return -1;
    conv_vfs(&s, d);
    return 0;
}

static int d_fstatvfs(int fd, struct dstatvfs *d)
{
    struct statvfs s;
    if (fstatvfs(fd, &s) < 0)
        return -1;
    conv_vfs(&s, d);
    return 0;
}

DSYM("_statvfs", d_statvfs);
DSYM("_fstatvfs", d_fstatvfs);
