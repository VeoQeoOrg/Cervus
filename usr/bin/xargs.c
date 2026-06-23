#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <cervus_util.h>

static const char USAGE[] =
    "Usage: xargs [-n max] [-I repl] [command [args...]]\nBuild and execute command lines from standard input.\n\n  -n N   use at most N arguments per command line\n  -I R   replace R in args with each input line\n";

#define MAX_ITEMS 512
#define MAX_ARGS  600

static int run(char **args)
{
    pid_t pid = fork();
    if (pid == 0) {
        execvp(args[0], args);
        fprintf(stderr, "xargs: cannot run '%s'\n", args[0]);
        _exit(127);
    }
    if (pid < 0) return 1;
    int st = 0;
    waitpid(pid, &st, 0);
    return WIFEXITED(st) ? WEXITSTATUS(st) : 1;
}

int main(int argc, char **argv)
{
    if (cervus_check_help_version(argc, argv, USAGE, "xargs")) return 0;

    int max_n = 0;
    const char *repl = NULL;
    int opt;
    while ((opt = getopt(argc, argv, "n:I:")) != -1) {
        if (opt == 'n') max_n = atoi(optarg);
        else if (opt == 'I') { repl = optarg; max_n = 1; }
        else { fputs(USAGE, stderr); return 1; }
    }

    char *cmd_argv[64];
    int cmd_argc = 0;
    if (optind >= argc) {
        cmd_argv[cmd_argc++] = "echo";
    } else {
        for (int i = optind; i < argc && cmd_argc < 60; i++)
            cmd_argv[cmd_argc++] = argv[i];
    }

    static char blob[65536];
    size_t total = 0;
    ssize_t n;
    while ((n = read(0, blob + total, sizeof(blob) - 1 - total)) > 0) {
        total += (size_t)n;
        if (total >= sizeof(blob) - 1) break;
    }
    blob[total] = '\0';

    char *items[MAX_ITEMS];
    int nitems = 0;
    char *p = blob;
    while (*p && nitems < MAX_ITEMS) {
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
        if (!*p) break;
        items[nitems++] = p;
        if (repl) {
            while (*p && *p != '\n') p++;
        } else {
            while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') p++;
        }
        if (*p) *p++ = '\0';
    }
    if (nitems == 0) return 0;

    int batch = max_n > 0 ? max_n : nitems;
    int rc = 0;

    for (int i = 0; i < nitems; i += batch) {
        char *args[MAX_ARGS];
        int na = 0;
        if (repl) {
            static char rbuf[64][512];
            int rn = 0;
            for (int j = 0; j < cmd_argc && na < MAX_ARGS - 1; j++) {
                const char *src = cmd_argv[j];
                const char *hit = strstr(src, repl);
                if (hit && rn < 64) {
                    snprintf(rbuf[rn], sizeof(rbuf[rn]), "%.*s%s%s",
                             (int)(hit - src), src, items[i], hit + strlen(repl));
                    args[na++] = rbuf[rn++];
                } else {
                    args[na++] = (char *)src;
                }
            }
        } else {
            for (int j = 0; j < cmd_argc && na < MAX_ARGS - 1; j++)
                args[na++] = cmd_argv[j];
            for (int j = i; j < nitems && j < i + batch && na < MAX_ARGS - 1; j++)
                args[na++] = items[j];
        }
        args[na] = NULL;
        int r = run(args);
        if (r) rc = r;
    }
    return rc;
}
