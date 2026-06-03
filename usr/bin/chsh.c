#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <cervus_util.h>

static const char USAGE[] =
    "Usage: chsh [SHELL]\n"
    "Change the default login shell (stored in /etc/shell).\n\n"
    "  chsh            print the current default shell\n"
    "  chsh -l         list available shells\n"
    "  chsh /bin/csh   set the default shell\n";

static const char *SHELL_FILE  = "/etc/shell";
static const char *SHELL_FILE2 = "/mnt/etc/shell";

static const char *KNOWN_SHELLS[] = { "/bin/csh", NULL };

static void print_current(void) {
    int fd = open(SHELL_FILE, O_RDONLY, 0);
    if (fd < 0) fd = open(SHELL_FILE2, O_RDONLY, 0);
    if (fd < 0) { puts("/bin/csh"); return; }
    char buf[256];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) { puts("/bin/csh"); return; }
    buf[n] = '\0';
    int i = 0;
    while (buf[i] && buf[i] != '\n' && buf[i] != '\r') i++;
    buf[i] = '\0';
    puts(buf[0] ? buf : "/bin/csh");
}

static int write_shell_file(const char *path, const char *shell) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return -1;
    size_t n = strlen(shell);
    write(fd, shell, n);
    write(fd, "\n", 1);
    close(fd);
    return 0;
}

int main(int argc, char **argv) {
    if (cervus_check_help_version(argc, argv, USAGE, "chsh")) return 0;

    if (argc < 2) { print_current(); return 0; }

    if (strcmp(argv[1], "-l") == 0) {
        for (int i = 0; KNOWN_SHELLS[i]; i++) {
            struct stat st;
            if (stat(KNOWN_SHELLS[i], &st) == 0 && st.st_type != 1)
                puts(KNOWN_SHELLS[i]);
        }
        return 0;
    }

    const char *shell = argv[1];
    if (shell[0] != '/') {
        fputs(C_RED "chsh: shell path must be absolute\n" C_RESET, stderr);
        return 1;
    }
    struct stat st;
    if (stat(shell, &st) != 0) {
        fprintf(stderr, C_RED "chsh: %s: no such file\n" C_RESET, shell);
        return 1;
    }
    if (st.st_type == 1) {
        fprintf(stderr, C_RED "chsh: %s: is a directory\n" C_RESET, shell);
        return 1;
    }

    int wrote = 0;
    if (write_shell_file(SHELL_FILE, shell) == 0) wrote = 1;
    {
        struct stat ms;
        if (stat("/mnt/etc", &ms) == 0 && ms.st_type == 1)
            if (write_shell_file(SHELL_FILE2, shell) == 0) wrote = 1;
    }
    if (!wrote) {
        fputs(C_RED "chsh: cannot write /etc/shell\n" C_RESET, stderr);
        return 1;
    }
    printf("Default shell set to %s\n", shell);
    return 0;
}
