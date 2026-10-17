#ifndef _LINUX_PRCTL_H
#define _LINUX_PRCTL_H

#define PR_SET_PDEATHSIG        1
#define PR_GET_PDEATHSIG        2
#define PR_GET_DUMPABLE         3
#define PR_SET_DUMPABLE         4
#define PR_GET_UNALIGN          5
#define PR_SET_UNALIGN          6
#define PR_GET_KEEPCAPS         7
#define PR_SET_KEEPCAPS         8
#define PR_GET_FPEMU            9
#define PR_SET_FPEMU           10
#define PR_GET_FPEXC           11
#define PR_SET_FPEXC           12
#define PR_GET_TIMING          13
#define PR_SET_TIMING          14
#define PR_SET_NAME            15
#define PR_GET_NAME            16
#define PR_GET_ENDIAN          19
#define PR_SET_ENDIAN          20
#define PR_GET_SECCOMP         21
#define PR_SET_SECCOMP         22
#define PR_CAPBSET_READ        23
#define PR_CAPBSET_DROP        24
#define PR_GET_TSC             25
#define PR_SET_TSC             26
#define PR_GET_SECUREBITS      27
#define PR_SET_SECUREBITS      28
#define PR_SET_TIMERSLACK      29
#define PR_GET_TIMERSLACK      30
#define PR_TASK_PERF_EVENTS_DISABLE 31
#define PR_TASK_PERF_EVENTS_ENABLE  32
#define PR_MCE_KILL            33
#define PR_MCE_KILL_GET        34
#define PR_SET_MM              35
#define PR_SET_CHILD_SUBREAPER 36
#define PR_GET_CHILD_SUBREAPER 37
#define PR_SET_NO_NEW_PRIVS    38
#define PR_GET_NO_NEW_PRIVS    39
#define PR_GET_TID_ADDRESS     40
#define PR_SET_THP_DISABLE     41
#define PR_GET_THP_DISABLE     42
#define PR_SET_FP_MODE         45
#define PR_GET_FP_MODE         46
#define PR_CAP_AMBIENT         47
#define PR_SET_SPECULATION_CTRL 53
#define PR_GET_SPECULATION_CTRL 52
#define PR_SET_SYSCALL_USER_DISPATCH 59
#define PR_SET_VMA             0x53564d41
#define PR_SET_PTRACER         0x59616d61
#define PR_SET_PTRACER_ANY     ((unsigned long)-1)

#define PR_SYS_DISPATCH_OFF    0
#define PR_SYS_DISPATCH_ON     1

#endif
