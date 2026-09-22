#ifndef _GRP_H
#define _GRP_H
#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <stddef.h>

struct group {
    char  *gr_name;
    char  *gr_passwd;
    gid_t  gr_gid;
    char **gr_mem;
};

struct group *getgrgid(gid_t gid);
struct group *getgrnam(const char *name);
void          setgrent(void);
struct group *getgrent(void);
void          endgrent(void);
int           getgrouplist(const char *user, gid_t group, gid_t *groups, int *ngroups);
int           initgroups(const char *user, gid_t group);
int           setgroups(size_t size, const gid_t *list);
int           getgroups(int size, gid_t *list);

#ifdef __cplusplus
}
#endif
#endif
