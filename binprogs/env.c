#include "../apps/cervus_user.h"

CERVUS_MAIN(env_main) {
    if (argc >= 2 && argv[1][0] != '-') {
        const char *val = getenv_argv(argc, argv, argv[1], (void *)0);
        if (val) {
            ws(val); wn();
            exit(0);
        } else {
            ws(C_RED "env: variable not set: " C_RESET);
            ws(argv[1]); wn();
            exit(1);
        }
    }

    int found = 0;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (a[0]=='-' && a[1]=='-' && a[2]=='e' && a[3]=='n' &&
            a[4]=='v' && a[5]==':') {
            ws(a + 6);
            wn();
            found++;
        }
    }
    if (!found) {
        ws(C_GRAY "(no environment variables set)" C_RESET "\n");
    }
    exit(0);
}