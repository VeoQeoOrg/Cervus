#ifndef _SYS_RESOURCE_H
#define _SYS_RESOURCE_H
#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <sys/time.h>

#define RUSAGE_SELF      0
#define RUSAGE_CHILDREN (-1)

struct rusage {
    struct timeval ru_utime;
    struct timeval ru_stime;
    long           ru_maxrss;
    long           ru_ixrss;
    long           ru_idrss;
    long           ru_isrss;
    long           ru_minflt;
    long           ru_majflt;
    long           ru_nswap;
    long           ru_inblock;
    long           ru_oublock;
    long           ru_msgsnd;
    long           ru_msgrcv;
    long           ru_nsignals;
    long           ru_nvcsw;
    long           ru_nivcsw;
};

int getrusage(int who, struct rusage *usage);

#define PRIO_PROCESS 0
#define PRIO_PGRP    1
#define PRIO_USER    2
int getpriority(int which, id_t who);
int setpriority(int which, id_t who, int prio);

typedef unsigned long rlim_t;
#define RLIM_INFINITY  (~0UL)
#define RLIM_SAVED_MAX RLIM_INFINITY
#define RLIM_SAVED_CUR RLIM_INFINITY
#define RLIMIT_CPU     0
#define RLIMIT_FSIZE   1
#define RLIMIT_DATA    2
#define RLIMIT_STACK   3
#define RLIMIT_CORE    4
#define RLIMIT_RSS     5
#define RLIMIT_NPROC   6
#define RLIMIT_NOFILE  7
#define RLIMIT_MEMLOCK 8
#define RLIMIT_AS      9
#define RLIMIT_NLIMITS 10
struct rlimit {
    rlim_t rlim_cur;
    rlim_t rlim_max;
};
int getrlimit(int resource, struct rlimit *rl);
int setrlimit(int resource, const struct rlimit *rl);

#ifdef __cplusplus
}
#endif
#endif
