#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <cervus_util.h>


static const char USAGE[] =
    "Usage: whoami\nPrint effective user name.\n";
int main(int argc, char **argv)
{
    if (cervus_check_help_version(argc, argv, USAGE, "whoami")) return 0;
    (void)argc; (void)argv;
    const char *u = getenv("USER");
    if (!u) u = "root";
    fputs(u, stdout);
    putchar('\n');
    return 0;
}
