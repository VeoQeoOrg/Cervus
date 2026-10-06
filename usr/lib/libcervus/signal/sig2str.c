#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const g_names[32] = {
    NULL, "HUP", "INT", "QUIT", "ILL", "TRAP", "ABRT", "BUS", "FPE", "KILL", "USR1",
    "SEGV", "USR2", "PIPE", "ALRM", "TERM", "STKFLT", "CHLD", "CONT", "STOP", "TSTP",
    "TTIN", "TTOU", "URG", "XCPU", "XFSZ", "VTALRM", "PROF", "WINCH", "IO", "PWR", "SYS",
};

static const struct { const char *name; int sig; } g_alias[] = {
    { "IOT", SIGABRT }, { "CLD", SIGCHLD }, { "POLL", SIGIO },
};

int sig2str(int sig, char *str)
{
    if (sig > 0 && sig < 32) {
        strcpy(str, g_names[sig]);
        return 0;
    }
    int lo = SIGRTMIN, hi = SIGRTMAX;
    if (sig < lo || sig > hi) return -1;
    if (sig == lo) strcpy(str, "RTMIN");
    else if (sig == hi) strcpy(str, "RTMAX");
    else if (sig - lo <= (hi - lo) / 2) snprintf(str, SIG2STR_MAX, "RTMIN+%d", sig - lo);
    else snprintf(str, SIG2STR_MAX, "RTMAX-%d", hi - sig);
    return 0;
}

static int parse_offset(const char *s, int *out)
{
    if (!*s) { *out = 0; return 0; }
    char *end;
    long v = strtol(s, &end, 10);
    if (*end || end == s) return -1;
    *out = (int)v;
    return 0;
}

int str2sig(const char *str, int *sig)
{
    if (!str || !*str) return -1;
    if (str[0] >= '0' && str[0] <= '9') {
        char *end;
        long v = strtol(str, &end, 10);
        if (*end || v <= 0 || v >= NSIG) return -1;
        char tmp[SIG2STR_MAX];
        if (sig2str((int)v, tmp) < 0) return -1;
        *sig = (int)v;
        return 0;
    }
    for (int i = 1; i < 32; i++)
        if (!strcmp(str, g_names[i])) { *sig = i; return 0; }
    for (size_t i = 0; i < sizeof g_alias / sizeof g_alias[0]; i++)
        if (!strcmp(str, g_alias[i].name)) { *sig = g_alias[i].sig; return 0; }
    int off, lo = SIGRTMIN, hi = SIGRTMAX;
    if (!strncmp(str, "RTMIN", 5)) {
        if (parse_offset(str + 5, &off) < 0 || (str[5] && str[5] != '+') || off < 0 || lo + off > hi) return -1;
        *sig = lo + off;
        return 0;
    }
    if (!strncmp(str, "RTMAX", 5)) {
        if (parse_offset(str + 5, &off) < 0 || (str[5] && str[5] != '-') || off > 0 || hi + off < lo) return -1;
        *sig = hi + off;
        return 0;
    }
    return -1;
}
