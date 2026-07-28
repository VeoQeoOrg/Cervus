#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <pwutil.h>
#include <sys/cervus.h>
#include <sys/syscall.h>
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
        if (isprint((unsigned char)c)) buf[i++] = c;
    }
    buf[i] = '\0';
    return strcmp(buf, "yes") == 0 || strcmp(buf, "y") == 0 ||
           strcmp(buf, "YES") == 0 || strcmp(buf, "Y") == 0;
}

static int elevate(const char *action)
{
    if (getuid() == 0) return 0;

    uint32_t myuid = (uint32_t)getuid();
    char uname[64];
    if (pw_lookup_uid(myuid, uname, sizeof(uname), NULL, 0, NULL, 0) != 0)
        snprintf(uname, sizeof(uname), "%u", myuid);

    char prompt[128];
    snprintf(prompt, sizeof(prompt), "[%s] password for %s: ", action, uname);

    char pw[256] = {0};
    if (pw_getpass(prompt, pw, sizeof(pw)) < 0) return -1;

    long r = syscall3(SYS_SUDO, (uint64_t)(uintptr_t)pw, 0, 0);
    memset(pw, 0, sizeof(pw));
    if (r != 0) {
        if (r == -1 || r == -13)
            fputs("reboot: authentication failure\n", stderr);
        else
            fputs("reboot: not permitted (you are not a sudoer)\n", stderr);
        return -1;
    }
    return 0;
}

static const char USAGE[] =
    "Usage: reboot\nReboot the system. Requires root or the root password.\n";
int main(int argc, char **argv)
{
    if (cervus_check_help_version(argc, argv, USAGE, "reboot")) return 0;
    (void)argc; (void)argv;
    fputs(C_YELLOW "[Reboot]" C_RESET "\n\n", stdout);
    fputs("Are you sure you want to " C_CYAN "reboot" C_RESET " the computer?\n", stdout);
    fputs("Type " C_BOLD "yes" C_RESET " to confirm, or anything else to cancel: ", stdout);

    if (!confirm_prompt()) {
        fputs(C_GREEN "\nReboot cancelled." C_RESET "\n", stdout);
        return 0;
    }
    putchar('\n');

    if (elevate("reboot") != 0)
        return 1;

    fputs(C_YELLOW "Rebooting..." C_RESET "\n", stdout);
    fputs(C_GRAY "Sending reboot signal..." C_RESET "\n", stdout);
    int ret = cervus_reboot();
    if (ret < 0) {
        fprintf(stderr, "reboot: failed (error %d)\n", ret);
        return 1;
    }
    return 0;
}
