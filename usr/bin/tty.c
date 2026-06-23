#include <stdio.h>
#include <unistd.h>
#include <cervus_util.h>

static const char USAGE[] =
    "Usage: tty [-s]\nPrint the file name of the terminal connected to standard input.\n\n  -s   silent, only return exit status\n";

int main(int argc, char **argv)
{
    if (cervus_check_help_version(argc, argv, USAGE, "tty")) return 0;

    int silent = 0;
    int opt;
    while ((opt = getopt(argc, argv, "s")) != -1) {
        if (opt == 's') silent = 1;
        else { fputs(USAGE, stderr); return 2; }
    }

    int is = isatty(0);
    if (!silent) puts(is ? "/dev/tty" : "not a tty");
    return is ? 0 : 1;
}
