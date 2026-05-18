#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <cervus_util.h>


static const char USAGE[] =
    "Usage: dirname path ...\nStrip last component from path.\n";
int main(int argc, char **argv)
{
    if (cervus_check_help_version(argc, argv, USAGE, "dirname")) return 0;
    const char *path = NULL;
    for (int i = 1; i < argc; i++) {
        if (is_shell_flag(argv[i])) continue;
        path = argv[i];
        break;
    }
    if (!path) {
        fputs(C_RED "usage: dirname <path>" C_RESET "\n", stderr);
        return 1;
    }

    char buf[512];
    strncpy(buf, path, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    int blen = (int)strlen(buf);
    while (blen > 1 && buf[blen - 1] == '/') buf[--blen] = '\0';

    char *slash = NULL;
    for (char *q = buf; *q; q++) if (*q == '/') slash = q;

    if (!slash) { fputs(".", stdout); putchar('\n'); return 0; }
    if (slash == buf) { fputs("/", stdout); putchar('\n'); return 0; }

    *slash = '\0';
    fputs(buf, stdout);
    putchar('\n');
    return 0;
}
