#ifndef _SYS_PRCTL_H
#define _SYS_PRCTL_H
#ifdef __cplusplus
extern "C" {
#endif

#include <linux/prctl.h>

int prctl(int option, ...);

#ifdef __cplusplus
}
#endif
#endif
