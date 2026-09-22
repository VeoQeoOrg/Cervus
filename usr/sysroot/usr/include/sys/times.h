#ifndef _SYS_TIMES_H
#define _SYS_TIMES_H
#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <time.h>

struct tms {
    clock_t tms_utime;
    clock_t tms_stime;
    clock_t tms_cutime;
    clock_t tms_cstime;
};

clock_t times(struct tms *buf);

#ifdef __cplusplus
}
#endif
#endif
