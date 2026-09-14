#include <unistd.h>
#include <grp.h>
#include <errno.h>

int initgroups(const char *user, gid_t group)
{
    (void)user;
    return setgid(group);
}

int setgroups(size_t size, const gid_t *list)
{
    (void)list;
    if (size > 1) { errno = EINVAL; return -1; }
    return 0;
}

int getgroups(int size, gid_t *list)
{
    if (size <= 0) return 1;
    if (!list) { errno = EFAULT; return -1; }
    list[0] = getgid();
    return 1;
}
