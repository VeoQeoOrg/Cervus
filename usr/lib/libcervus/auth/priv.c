#include <pwutil.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

static int    g_argc;
static char **g_argv;

void priv_argv(int argc, char **argv)
{
    g_argc = argc;
    g_argv = argv;
}

int priv_is_root(void)
{
    return getuid() == 0;
}

static const char *prog_name(void)
{
    const char *p = (g_argc > 0 && g_argv && g_argv[0]) ? g_argv[0] : "this";
    const char *slash = strrchr(p, '/');
    return slash ? slash + 1 : p;
}

void priv_denied(const char *path)
{
    const char *prog = prog_name();
    int refused = (errno == EACCES || errno == EPERM);

    if (refused && !priv_is_root()) {
        fprintf(stderr, "%s: %s belongs to root, so nothing was saved\n", prog, path);
        fprintf(stderr, "%s: to keep the change, run\n    sudo %s", prog, prog);
        for (int i = 1; i < g_argc; i++) fprintf(stderr, " %s", g_argv[i]);
        fputc('\n', stderr);
        return;
    }
    fprintf(stderr, "%s: cannot write %s: %s\n", prog, path, strerror(errno));
}

int priv_require_root(const char *doing)
{
    if (priv_is_root()) return 0;
    const char *prog = prog_name();
    fprintf(stderr, "%s: %s needs root\n", prog, doing);
    fprintf(stderr, "%s: run\n    sudo %s", prog, prog);
    for (int i = 1; i < g_argc; i++) fprintf(stderr, " %s", g_argv[i]);
    fputc('\n', stderr);
    return -1;
}
