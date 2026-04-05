#include "../apps/cervus_user.h"

CERVUS_MAIN(main) {
    (void)argc;
    (void)argv;

    ws(C_YELLOW "=== Reboot ===" C_RESET "\n\n");
    ws("Are you sure you want to " C_CYAN "reboot" C_RESET " the computer?\n");
    ws("Type " C_BOLD "yes" C_RESET " to confirm, or anything else to cancel: ");

    char buf[64];
    int i = 0;
    while (i < 63) {
        char c;
        ssize_t r = read(0, &c, 1);
        if (r <= 0) break;
        if (c == '\n' || c == '\r') break;
        if (c == '\b' || c == 0x7F) {
            if (i > 0) {
                i--;
                ws("\b \b");
            }
            continue;
        }
        if (c >= 0x20 && c < 0x7F) {
            buf[i++] = c;
            write(1, &c, 1);
        }
    }
    buf[i] = '\0';
    wn();

    if (strcmp(buf, "yes") == 0 || strcmp(buf, "y") == 0 ||
        strcmp(buf, "YES") == 0 || strcmp(buf, "Y") == 0) {
        wn();
        ws(C_YELLOW "Rebooting Cervus OS..." C_RESET "\n");
        ws(C_GRAY "Sending reboot signal..." C_RESET "\n");
        int ret = cervus_reboot();
        if (ret < 0) {
            ws(C_RED "Reboot failed (error " C_RESET);
            print_i64((int64_t)ret);
            ws(C_RED ")" C_RESET "\n");
            ws("You may need root privileges.\n");
        }
    } else {
        ws(C_GREEN "Reboot cancelled." C_RESET "\n");
    }
}