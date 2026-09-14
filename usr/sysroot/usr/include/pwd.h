#ifndef _PWD_H
#define _PWD_H

#include <sys/types.h>

struct passwd {
    char   *pw_name;
    char   *pw_passwd;
    uid_t   pw_uid;
    gid_t   pw_gid;
    char   *pw_gecos;
    char   *pw_dir;
    char   *pw_shell;
};

struct passwd *getpwuid(uid_t uid);
struct passwd *getpwnam(const char *name);
void           setpwent(void);
struct passwd *getpwent(void);
void           endpwent(void);

#endif
