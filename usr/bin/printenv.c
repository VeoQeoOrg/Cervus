#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cervus_util.h>

extern char **environ;

static const char USAGE[] =
    "Usage: printenv [-0] [name ...]\n"
    "Print the values of the named environment variables, or all of them.\n\n"
    "  -0   end each entry with NUL instead of newline\n";

int main(int argc, char **argv)
{
    int i = 1, nul = 0;
    for (; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        if (!strcmp(argv[i], "--")) { i++; break; }
        if (!strcmp(argv[i], "--help")) { fputs(USAGE, stdout); return 0; }
        if (!strcmp(argv[i], "--version")) { puts("printenv (" CERVUS_VERSION_STR ")"); return 0; }
        if (!strcmp(argv[i], "-0") || !strcmp(argv[i], "--null")) { nul = 1; continue; }
        fputs(USAGE, stderr);
        return 2;
    }
    char end = nul ? '\0' : '\n';
    if (i >= argc) {
        for (char **e = environ; e && *e; e++) { fputs(*e, stdout); putchar(end); }
        return 0;
    }
    int missing = 0;
    for (; i < argc; i++) {
        const char *v = getenv(argv[i]);
        if (!v) { missing = 1; continue; }
        fputs(v, stdout);
        putchar(end);
    }
    return missing;
}
