#include <unistd.h>
#include <errno.h>
#include <libcervus.h>

int seteuid(uid_t uid)
{
    return setuid(uid);
}

int setegid(gid_t gid)
{
    return setgid(gid);
}

int setreuid(uid_t ruid, uid_t euid)
{
    if (euid != (uid_t)-1) return setuid(euid);
    if (ruid != (uid_t)-1) return setuid(ruid);
    return 0;
}

int setregid(gid_t rgid, gid_t egid)
{
    if (egid != (gid_t)-1) return setgid(egid);
    if (rgid != (gid_t)-1) return setgid(rgid);
    return 0;
}

int setresuid(uid_t ruid, uid_t euid, uid_t suid)
{
    uid_t target = euid != (uid_t)-1 ? euid : ruid != (uid_t)-1 ? ruid : suid;
    return target == (uid_t)-1 ? 0 : setuid(target);
}

int setresgid(gid_t rgid, gid_t egid, gid_t sgid)
{
    gid_t target = egid != (gid_t)-1 ? egid : rgid != (gid_t)-1 ? rgid : sgid;
    return target == (gid_t)-1 ? 0 : setgid(target);
}

int getresuid(uid_t *ruid, uid_t *euid, uid_t *suid)
{
    uid_t r = getuid(), e = geteuid();
    if (ruid) *ruid = r;
    if (euid) *euid = e;
    if (suid) *suid = e;
    return 0;
}

int getresgid(gid_t *rgid, gid_t *egid, gid_t *sgid)
{
    gid_t r = getgid(), e = getegid();
    if (rgid) *rgid = r;
    if (egid) *egid = e;
    if (sgid) *sgid = e;
    return 0;
}
