#include <stdio.h>
#include <cervus_util.h>

int main(int argc, char **argv)
{
    const char *msg = "y";
    for (int i = 1; i < argc; i++) {
        if (is_shell_flag(argv[i])) continue;
        msg = argv[i];
        break;
    }
    for (;;) puts(msg);
}
