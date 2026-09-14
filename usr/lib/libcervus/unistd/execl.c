#include <unistd.h>
#include <stdarg.h>
#include <stdlib.h>
#include <errno.h>

#define EXEC_MAX_ARGS 256

static int collect(const char *arg0, va_list ap, char **argv, int cap, char ***envp_out)
{
    int n = 0;
    argv[n++] = (char *)arg0;
    while (n < cap - 1) {
        char *a = va_arg(ap, char *);
        argv[n++] = a;
        if (!a) break;
    }
    argv[cap - 1] = NULL;
    if (envp_out) *envp_out = va_arg(ap, char **);
    return n;
}

int execl(const char *path, const char *arg0, ...)
{
    char *argv[EXEC_MAX_ARGS];
    va_list ap;
    va_start(ap, arg0);
    collect(arg0, ap, argv, EXEC_MAX_ARGS, NULL);
    va_end(ap);
    return execv(path, argv);
}

int execlp(const char *file, const char *arg0, ...)
{
    char *argv[EXEC_MAX_ARGS];
    va_list ap;
    va_start(ap, arg0);
    collect(arg0, ap, argv, EXEC_MAX_ARGS, NULL);
    va_end(ap);
    return execvp(file, argv);
}

int execle(const char *path, const char *arg0, ...)
{
    char *argv[EXEC_MAX_ARGS];
    char **envp = NULL;
    va_list ap;
    va_start(ap, arg0);
    collect(arg0, ap, argv, EXEC_MAX_ARGS, &envp);
    va_end(ap);
    return execve(path, argv, envp);
}
