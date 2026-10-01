#include <sys/statfs.h>
#include <sys/statvfs.h>
#include <string.h>

static void from_vfs(const struct statvfs *v, struct statfs *s)
{
    memset(s, 0, sizeof *s);
    s->f_type = 0xC3E705;
    s->f_bsize = (long)v->f_bsize;
    s->f_frsize = (long)(v->f_frsize ? v->f_frsize : v->f_bsize);
    s->f_blocks = v->f_blocks;
    s->f_bfree = v->f_bfree;
    s->f_bavail = v->f_bavail;
    s->f_files = v->f_files;
    s->f_ffree = v->f_ffree;
    s->f_fsid.__val[0] = (int)v->f_fsid;
    s->f_namelen = (long)v->f_namemax;
    s->f_flags = (long)v->f_flag;
}

int statfs(const char *path, struct statfs *buf)
{
    struct statvfs v;
    if (statvfs(path, &v) != 0) return -1;
    from_vfs(&v, buf);
    return 0;
}

int fstatfs(int fd, struct statfs *buf)
{
    struct statvfs v;
    if (fstatvfs(fd, &v) != 0) return -1;
    from_vfs(&v, buf);
    return 0;
}
