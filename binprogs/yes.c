#include "../apps/cervus_user.h"

CERVUS_MAIN(yes_main) {
    const char *msg = "y";
    for (int i = 1; i < argc; i++) {
        if (is_shell_flag(argv[i])) continue;
        msg = argv[i]; break;
    }
    for(;;){ ws(msg); write(1,"\n",1); }
}