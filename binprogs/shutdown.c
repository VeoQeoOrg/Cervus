#include "../apps/cervus_user.h"

static int confirm_prompt(void) {
    char buf[64]; int i = 0;
    while (i < 63) {
        char c;
        ssize_t r = read(0, &c, 1);
        if (r <= 0) break;
        if (c == '\n' || c == '\r') break;
        if (c == '\b' || c == 0x7F) {
            if (i > 0) { i--; ws("\b \b"); }
            continue;
        }
        if (isprint((unsigned char)c)) { buf[i++] = c; wc(c); }
    }
    buf[i] = '\0';
    wn();
    return strcmp(buf, "yes") == 0 || strcmp(buf, "y")   == 0 ||
           strcmp(buf, "YES") == 0 || strcmp(buf, "Y")   == 0;
}

CERVUS_MAIN(main) {
    (void)argc; (void)argv;
    ws(C_YELLOW "=== Shutdown ===" C_RESET "\n\n");
    ws("Are you sure you want to " C_RED "shut down" C_RESET " the computer?\n");
    ws("Type " C_BOLD "yes" C_RESET " to confirm, or anything else to cancel: ");

    if (confirm_prompt()) {
        wn();
        ws(C_YELLOW "Shutting down Cervus OS..." C_RESET "\n");
        ws(C_GRAY "Sending ACPI shutdown signal..." C_RESET "\n");
        int ret = cervus_shutdown();
        if (ret < 0) {
            fprintf(2, "shutdown: failed (error %d)\n", ret);
            ws("You may need root privileges.\n");
        }
    } else {
        ws(C_GREEN "Shutdown cancelled." C_RESET "\n");
    }
}