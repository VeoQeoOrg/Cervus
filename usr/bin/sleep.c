#include <stdio.h>
#include <stdlib.h>
#include <sys/cervus.h>
#include <cervus_util.h>

int main(int argc, char **argv)
{
    const char *arg = NULL;
    for (int i = 1; i < argc; i++) {
        if (is_shell_flag(argv[i])) continue;
        arg = argv[i];
        break;
    }
    if (!arg) {
        fputs("Usage: sleep <seconds>\n", stderr);
        return 1;
    }
    unsigned long secs = strtoul(arg, NULL, 10);
    cervus_nanosleep((uint64_t)secs * 1000000000ULL);
    return 0;
}
