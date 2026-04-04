#include "../apps/cervus_user.h"

CERVUS_MAIN(clear_main) {
    (void)argc; (void)argv;
    ws("\x1b[2J\x1b[H");
    exit(0);
}