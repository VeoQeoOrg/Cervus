#include <stdio.h>

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    fputs("\x1b[2J\x1b[H", stdout);
    return 0;
}
