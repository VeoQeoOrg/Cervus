#include "../apps/cervus_user.h"

CERVUS_MAIN(pwd_main) {
    const char *cwd = get_cwd_flag(argc, argv);
    if (!cwd || cwd[0] == '\0') cwd = "/";
    ws(cwd); wn();
    exit(0);
}