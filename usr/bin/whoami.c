#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <cervus_util.h>

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    const char *u = getenv_argv(argc, argv, "USER", NULL);
    if (!u) u = "root";
    fputs(u, stdout);
    putchar('\n');
    return 0;
}
