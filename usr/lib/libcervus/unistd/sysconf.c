#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

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

long sysconf(int name)
{
    switch (name) {
        case _SC_PAGESIZE:          return 4096;
        case _SC_OPEN_MAX:          return 256;
        case _SC_CLK_TCK:           return 1000000;
        case _SC_ARG_MAX:           return 131072;
        case _SC_NGROUPS_MAX:       return 32;
        case _SC_LINE_MAX:          return 2048;
        case _SC_SYMLOOP_MAX:       return 32;
        case _SC_HOST_NAME_MAX:     return 64;
        case _SC_LOGIN_NAME_MAX:    return 64;
        case _SC_NPROCESSORS_CONF:  return proc_field("/proc/cpuinfo", "cpus", 1);
        case _SC_NPROCESSORS_ONLN:  return proc_field("/proc/cpuinfo", "online", 1);
        case _SC_PHYS_PAGES:        return proc_field("/proc/meminfo", "MemTotal", 0) / 4;
        case _SC_AVPHYS_PAGES:      return proc_field("/proc/meminfo", "MemFree", 0) / 4;
        default: errno = EINVAL; return -1;
    }
}
