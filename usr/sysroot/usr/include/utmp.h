#ifndef _UTMP_H
#define _UTMP_H
#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <sys/time.h>
#include <stdint.h>

#define UT_LINESIZE 32
#define UT_NAMESIZE 32
#define UT_HOSTSIZE 256

#define EMPTY         0
#define RUN_LVL       1
#define BOOT_TIME     2
#define NEW_TIME      3
#define OLD_TIME      4
#define INIT_PROCESS  5
#define LOGIN_PROCESS 6
#define USER_PROCESS  7
#define DEAD_PROCESS  8
#define ACCOUNTING    9

#define _PATH_UTMP "/var/run/utmp"
#define _PATH_WTMP "/var/log/wtmp"
#define UTMP_FILE  _PATH_UTMP
#define WTMP_FILE  _PATH_WTMP

struct exit_status {
    short e_termination;
    short e_exit;
};

struct utmp {
    short              ut_type;
    pid_t              ut_pid;
    char               ut_line[UT_LINESIZE];
    char               ut_id[4];
    char               ut_user[UT_NAMESIZE];
    char               ut_host[UT_HOSTSIZE];
    struct exit_status ut_exit;
    int32_t            ut_session;
    struct {
        int32_t tv_sec;
        int32_t tv_usec;
    } ut_tv;
    int32_t            ut_addr_v6[4];
    char               __unused[20];
};

#define ut_name ut_user
#define ut_time ut_tv.tv_sec

void          setutent(void);
struct utmp  *getutent(void);
struct utmp  *getutid(const struct utmp *ut);
struct utmp  *getutline(const struct utmp *ut);
struct utmp  *pututline(const struct utmp *ut);
void          endutent(void);
int           utmpname(const char *file);

#ifdef __cplusplus
}
#endif
#endif
