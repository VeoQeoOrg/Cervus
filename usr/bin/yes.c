#include <stdio.h>
#include <cervus_util.h>


static const char USAGE[] =
    "Usage: yes [string]\nRepeatedly output STRING (default 'y') until killed.\n";
int main(int argc, char **argv)
{
    if (cervus_check_help_version(argc, argv, USAGE, "yes")) return 0;
    const char *msg = "y";
    for (int i = 1; i < argc; i++) {
        if (is_shell_flag(argv[i])) continue;
        msg = argv[i];
        break;
    }
    for (;;) puts(msg);
}
