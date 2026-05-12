#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/cervus.h>
#include <cervus_util.h>

static int confirm_prompt(void)
{
    char buf[64];
    int i = 0;
    while (i < 63) {
        char c;
        ssize_t r = read(0, &c, 1);
        if (r <= 0) break;
        if (c == '\n' || c == '\r') break;
        if (c == '\b' || c == 0x7F) {
            if (i > 0) { i--; fputs("\b \b", stdout); }
            continue;
        }
        if (isprint((unsigned char)c)) { buf[i++] = c; putchar(c); }
    }
    buf[i] = '\0';
    putchar('\n');
    return strcmp(buf, "yes") == 0 || strcmp(buf, "y") == 0 ||
           strcmp(buf, "YES") == 0 || strcmp(buf, "Y") == 0;
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    fputs(C_YELLOW "=== Shutdown ===" C_RESET "\n\n", stdout);
    fputs("Are you sure you want to " C_RED "shut down" C_RESET " the computer?\n", stdout);
    fputs("Type " C_BOLD "yes" C_RESET " to confirm, or anything else to cancel: ", stdout);

    if (confirm_prompt()) {
        putchar('\n');
        fputs(C_YELLOW "Shutting down Cervus OS..." C_RESET "\n", stdout);
        fputs(C_GRAY "Sending ACPI shutdown signal..." C_RESET "\n", stdout);
        int ret = cervus_shutdown();
        if (ret < 0) {
            fprintf(stderr, "shutdown: failed (error %d)\n", ret);
            return 1;
        }
    } else {
        fputs(C_GREEN "Shutdown cancelled." C_RESET "\n", stdout);
    }
    return 0;
}
