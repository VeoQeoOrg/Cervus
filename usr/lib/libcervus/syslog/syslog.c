#include <syslog.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/syscall.h>

static char g_ident[64];
static int  g_option;
static int  g_facility = LOG_USER;
static int  g_mask = 0xff;

void openlog(const char *ident, int option, int facility)
{
    snprintf(g_ident, sizeof g_ident, "%s", ident ? ident : "");
    g_option = option;
    if (facility) g_facility = facility;
}

void closelog(void)
{
    g_ident[0] = '\0';
    g_option = 0;
    g_facility = LOG_USER;
}

int setlogmask(int mask)
{
    int old = g_mask;
    if (mask) g_mask = mask;
    return old;
}

void vsyslog(int priority, const char *format, va_list ap)
{
    int pri = LOG_PRI(priority);
    if (!(g_mask & LOG_MASK(pri))) return;

    char body[1024];
    vsnprintf(body, sizeof body, format, ap);

    char line[1200];
    if (g_ident[0] && (g_option & LOG_PID))
        snprintf(line, sizeof line, "%s[%d]: %s", g_ident, (int)getpid(), body);
    else if (g_ident[0])
        snprintf(line, sizeof line, "%s: %s", g_ident, body);
    else
        snprintf(line, sizeof line, "%s", body);

    syscall4(SYS_KLOG, 3, 0, (unsigned long)line, strlen(line));
    if (g_option & LOG_PERROR) fprintf(stderr, "%s\n", line);
}

void syslog(int priority, const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    vsyslog(priority, format, ap);
    va_end(ap);
}
