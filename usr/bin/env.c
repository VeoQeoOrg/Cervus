#include <stdio.h>
#include <string.h>
#include <cervus_util.h>

int main(int argc, char **argv)
{
    if (argc >= 2 && !is_shell_flag(argv[1]) && argv[1][0] != '-') {
        const char *val = getenv_argv(argc, argv, argv[1], NULL);
        if (val) {
            puts(val);
            return 0;
        }
        fprintf(stderr, "env: variable not set: %s\n", argv[1]);
        return 1;
    }

    int found = 0;
    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--env:", 6) == 0) {
            puts(argv[i] + 6);
            found++;
        }
    }
    if (!found) fputs(C_GRAY "(no environment variables set)" C_RESET "\n", stdout);
    return 0;
}
