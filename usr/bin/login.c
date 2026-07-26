#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <pwutil.h>
#include <sys/syscall.h>

extern char **environ;

static int read_line(const char *prompt, char *buf, int cap) {
    fputs(prompt, stdout);
    fflush(stdout);
    int i = 0;
    char c;
    while (i < cap - 1) {
        int n = read(0, &c, 1);
        if (n <= 0) return -1;
        if (c == '\n' || c == '\r') break;
        if ((c == 8 || c == 127) && i > 0) { i--; continue; }
        if (c >= 32) buf[i++] = c;
    }
    buf[i] = 0;
    return i;
}

static int uid_has_password(uint32_t uid) {
    int fd = open("/etc/shadow", O_RDONLY, 0);
    if (fd < 0) return 0;
    char buf[4096];
    int n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return 0;
    buf[n] = 0;
    char pfx[24];
    int pl = snprintf(pfx, sizeof(pfx), "%u:", uid);
    const char *p = buf;
    while (*p) {
        if (strncmp(p, pfx, pl) == 0) {
            const char *q = p + pl;
            return (*q && *q != '!' && *q != '*' && *q != '\n');
        }
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
    }
    return 0;
}

static void read_hostname(char *out, int cap) {
    strncpy(out, "cervus", cap - 1); out[cap - 1] = 0;
    int fd = open("/etc/hostname", O_RDONLY, 0);
    if (fd < 0) return;
    char buf[64];
    int n = (int)read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return;
    int i = 0;
    while (i < n && i < cap - 1 && buf[i] != '\n' && buf[i] != '\r' && buf[i] != ' ') { out[i] = buf[i]; i++; }
    out[i] = 0;
    if (i == 0) { strncpy(out, "cervus", cap - 1); out[cap - 1] = 0; }
}

static int early_boot(void) {
    int fd = open("/proc/uptime", O_RDONLY, 0);
    if (fd < 0) return 1;
    char b[32];
    int n = (int)read(fd, b, sizeof(b) - 1);
    close(fd);
    if (n <= 0) return 1;
    b[n] = 0;
    long secs = 0;
    for (char *p = b; *p >= '0' && *p <= '9'; p++) secs = secs * 10 + (*p - '0');
    return secs < 5;
}

int main(void) {
    if (early_boot()) usleep(350000);
    char host[64];
    read_hostname(host, sizeof(host));
    fputs("\x1b[2J\x1b[H", stdout);
    printf("Cervus OS on \x1b[1;32m%s\x1b[0m\n", host);
    fflush(stdout);

    for (;;) {
        char name[64];
        if (read_line("\ncervus login: ", name, sizeof(name)) < 0) { sleep(1); continue; }
        if (!name[0]) continue;

        uint32_t uid = 0, gid = 0;
        char home[128] = "/", shell[128] = "/bin/csh";
        int known = (pw_lookup_name(name, &uid, &gid, home, sizeof(home), shell, sizeof(shell)) == 0);

        int has_pw = known ? uid_has_password(uid) : 0;
        int ok = 0;

        if (has_pw) {
            char pw[256];
            if (pw_getpass("Password: ", pw, sizeof(pw)) < 0) continue;
            ok = (known && syscall2(SYS_AUTH, (uint64_t)uid, (uint64_t)(uintptr_t)pw) == 0);
            memset(pw, 0, sizeof(pw));
        } else if (known && uid == 0) {
            fputs("\x1b[1;33mWARNING: root has no password. Set one with 'passwd'.\x1b[0m\n", stdout);
            ok = 1;
        } else {
            char pw[256];
            pw_getpass("Password: ", pw, sizeof(pw));
            memset(pw, 0, sizeof(pw));
            ok = 0;
        }

        if (!ok) {
            fputs("Login incorrect\n", stdout);
            fflush(stdout);
            continue;
        }

        if (!shell[0]) strcpy(shell, "/bin/csh");
        setenv("USER", name, 1);
        setenv("LOGNAME", name, 1);
        setenv("HOME", home[0] ? home : "/", 1);
        setenv("SHELL", shell, 1);
        chdir(home[0] ? home : "/");

        char *argv[] = { shell, NULL };
        execve(shell, argv, environ);
        fputs("login: cannot exec shell\n", stdout);
        return 1;
    }
    return 0;
}
