#include <stdio.h>
#include <cervus_util.h>


static const char USAGE[] =
    "Usage: clear\nClear the terminal screen.\n";
int main(int argc, char **argv)
{
    if (cervus_check_help_version(argc, argv, USAGE, "clear")) return 0;
    (void)argc; (void)argv;
    fputs("\x1b[2J\x1b[H", stdout);
    return 0;
}
