#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <pthread.h>
#include <sys/resource.h>

static long proc_field(const char *path, const char *key, long fallback)
{
    FILE *f = fopen(path, "r");
    if (!f) return fallback;
    char line[128];
    long value = fallback;
    size_t klen = strlen(key);
    while (fgets(line, sizeof line, f)) {
        if (strncmp(line, key, klen) != 0) continue;
        const char *p = line + klen;
        while (*p == ' ' || *p == '\t' || *p == ':') p++;
        long v = 0;
        int digits = 0;
        while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; digits++; }
        if (digits) value = v;
        break;
    }
    fclose(f);
    return value;
}

static long rlimit_value(int res)
{
    struct rlimit rl;
    if (getrlimit(res, &rl) < 0 || rl.rlim_cur == RLIM_INFINITY) return -1;
    return rl.rlim_cur > LONG_MAX ? LONG_MAX : (long)rl.rlim_cur;
}

long sysconf(int name)
{
    switch (name) {
    case _SC_ARG_MAX:               return 2L << 20;
    case _SC_CHILD_MAX:             return rlimit_value(RLIMIT_NPROC);
    case _SC_CLK_TCK:               return 1000000;
    case _SC_NGROUPS_MAX:           return 32;
    case _SC_OPEN_MAX:              return rlimit_value(RLIMIT_NOFILE);
    case _SC_STREAM_MAX:            return FOPEN_MAX;
    case _SC_TZNAME_MAX:            return -1;
    case _SC_PAGESIZE:              return 4096;
    case _SC_RTSIG_MAX:             return SIGRTMAX - SIGRTMIN + 1;
    case _SC_SIGQUEUE_MAX:          return rlimit_value(RLIMIT_SIGPENDING);
    case _SC_SEM_NSEMS_MAX:         return -1;
    case _SC_SEM_VALUE_MAX:         return INT_MAX;
    case _SC_IOV_MAX:               return 1024;
    case _SC_LINE_MAX:              return 2048;
    case _SC_RE_DUP_MAX:            return 255;
    case _SC_EXPR_NEST_MAX:         return 32;
    case _SC_BC_BASE_MAX:           return 99;
    case _SC_BC_DIM_MAX:            return 2048;
    case _SC_BC_SCALE_MAX:          return 99;
    case _SC_BC_STRING_MAX:         return 1000;
    case _SC_COLL_WEIGHTS_MAX:      return 255;
    case _SC_CHARCLASS_NAME_MAX:    return 2048;
    case _SC_GETGR_R_SIZE_MAX:      return 1024;
    case _SC_GETPW_R_SIZE_MAX:      return 1024;
    case _SC_LOGIN_NAME_MAX:        return 256;
    case _SC_TTY_NAME_MAX:          return 32;
    case _SC_HOST_NAME_MAX:         return 64;
    case _SC_SYMLOOP_MAX:           return 40;
    case _SC_THREAD_DESTRUCTOR_ITERATIONS: return 4;
    case _SC_THREAD_KEYS_MAX:       return PTHREAD_KEYS_MAX;
    case _SC_THREAD_STACK_MIN:      return 16384;
    case _SC_THREAD_THREADS_MAX:    return -1;
    case _SC_ATEXIT_MAX:            return INT_MAX;
    case _SC_PASS_MAX:              return 8192;
    case _SC_NZERO:                 return 20;
    case _SC_MINSIGSTKSZ:           return MINSIGSTKSZ;
    case _SC_SIGSTKSZ:              return SIGSTKSZ;
    case _SC_NPROCESSORS_CONF:      return proc_field("/proc/cpuinfo", "cpus", 1);
    case _SC_NPROCESSORS_ONLN:      return proc_field("/proc/cpuinfo", "online", 1);
    case _SC_PHYS_PAGES:            return proc_field("/proc/meminfo", "MemTotal", 0) / 4;
    case _SC_AVPHYS_PAGES:          return proc_field("/proc/meminfo", "MemFree", 0) / 4;

    case _SC_VERSION:
    case _SC_2_VERSION:
    case _SC_2_C_BIND:
    case _SC_2_C_DEV:
    case _SC_2_SW_DEV:
    case _SC_2_CHAR_TERM:
    case _SC_REALTIME_SIGNALS:
    case _SC_PRIORITY_SCHEDULING:
    case _SC_FSYNC:
    case _SC_MAPPED_FILES:
    case _SC_MEMLOCK:
    case _SC_MEMLOCK_RANGE:
    case _SC_MEMORY_PROTECTION:
    case _SC_SEMAPHORES:
    case _SC_SHARED_MEMORY_OBJECTS:
    case _SC_THREADS:
    case _SC_THREAD_SAFE_FUNCTIONS:
    case _SC_THREAD_ATTR_STACKSIZE:
    case _SC_THREAD_PRIORITY_SCHEDULING:
    case _SC_ADVISORY_INFO:
    case _SC_CLOCK_SELECTION:
    case _SC_MONOTONIC_CLOCK:
    case _SC_READER_WRITER_LOCKS:
    case _SC_SPAWN:
                                    return 200809L;
    case _SC_XOPEN_VERSION:         return 700;
    case _SC_XOPEN_XCU_VERSION:     return 4;
    case _SC_XOPEN_UNIX:
    case _SC_XOPEN_ENH_I18N:
    case _SC_XOPEN_SHM:
    case _SC_POLL:
    case _SC_SELECT:
    case _SC_REGEXP:
    case _SC_SHELL:
    case _SC_XBS5_LP64_OFF64:
    case _SC_V6_LP64_OFF64:
    case _SC_V7_LP64_OFF64:
                                    return 1;
    case _SC_CHAR_BIT:              return CHAR_BIT;
    case _SC_CHAR_MAX:              return CHAR_MAX;
    case _SC_CHAR_MIN:              return CHAR_MIN;
    case _SC_INT_MAX:               return INT_MAX;
    case _SC_INT_MIN:               return INT_MIN;
    case _SC_LONG_BIT:              return 64;
    case _SC_WORD_BIT:              return 32;
    case _SC_MB_LEN_MAX:            return MB_LEN_MAX;
    case _SC_SSIZE_MAX:             return LONG_MAX;
    case _SC_SCHAR_MAX:             return SCHAR_MAX;
    case _SC_SCHAR_MIN:             return SCHAR_MIN;
    case _SC_SHRT_MAX:              return SHRT_MAX;
    case _SC_SHRT_MIN:              return SHRT_MIN;
    case _SC_UCHAR_MAX:             return UCHAR_MAX;
    case _SC_UINT_MAX:              return (long)UINT_MAX;
    case _SC_ULONG_MAX:             return -1;
    case _SC_USHRT_MAX:             return USHRT_MAX;
    case _SC_NL_ARGMAX:             return 4096;
    case _SC_NL_LANGMAX:            return 2048;
    case _SC_NL_MSGMAX:
    case _SC_NL_NMAX:
    case _SC_NL_SETMAX:
    case _SC_NL_TEXTMAX:            return INT_MAX;
    }
    if (name >= 0 && name <= _SC_SIGSTKSZ) return -1;
    errno = EINVAL;
    return -1;
}
