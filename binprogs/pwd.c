#include "../apps/cervus_user.h"

CERVUS_MAIN(pwd_main) {
    const char *cwd = (argc > 1) ? argv[1] : "/";
    ws(cwd); wn();
    exit(0);
}