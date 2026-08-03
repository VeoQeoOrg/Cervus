#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <termios.h>
#include <pty.h>

#define TIOCSNONBLOCK 0x5481
#define MAXCLIENTS 8
#define WPORT 2300

extern char **environ;

static struct termios g_oldt;
static int g_have_tty = 0;

static void raw_console(void) {
    if (tcgetattr(0, &g_oldt) == 0) {
        g_have_tty = 1;
        struct termios raw = g_oldt;
        cfmakeraw(&raw);
        tcsetattr(0, 0, &raw);
    }
    int v = 1; ioctl(0, TIOCSNONBLOCK, &v);
}
static void restore_console(void) {
    int v = 0; ioctl(0, TIOCSNONBLOCK, &v);
    if (g_have_tty) tcsetattr(0, 0, &g_oldt);
}
static void nb(int fd) { long fl = fcntl(fd, F_GETFL, 0); fcntl(fd, F_SETFL, fl | O_NONBLOCK); }

static int host_mode(int port) {
    const char *shell = getenv("SHELL"); if (!shell || !shell[0]) shell = "/bin/csh";
    int master, slave;
    if (openpty(&master, &slave)) { printf("wterm: openpty failed\n"); return 1; }
    { struct winsize ws; if (ioctl(1, TIOCGWINSZ, &ws) == 0) ioctl(master, TIOCSWINSZ, &ws); }

    pid_t pid = fork();
    if (pid == 0) {
        close(master);
        setsid();
        dup2(slave, 0); dup2(slave, 1); dup2(slave, 2);
        if (slave > 2) close(slave);
        char *av[] = { (char *)shell, NULL };
        execve(shell, av, environ);
        _exit(127);
    }
    close(slave);

    int ls = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a; memset(&a, 0, sizeof a);
    a.sin_family = AF_INET; a.sin_port = htons((uint16_t)port); a.sin_addr.s_addr = INADDR_ANY;
    if (bind(ls, (struct sockaddr *)&a, sizeof a) < 0 || listen(ls, 4) < 0) {
        printf("wterm: bind/listen :%d failed\n", port); return 1;
    }
    printf("wterm: hosting shared terminal on port %d (attach: wterm <this-ip>)\n", port);

    int clients[MAXCLIENTS]; for (int i = 0; i < MAXCLIENTS; i++) clients[i] = -1;
    int one = 1; ioctl(master, TIOCSNONBLOCK, &one);
    nb(master); nb(ls);
    raw_console();

    uint8_t buf[4096];
    int done = 0;
    while (!done) {
        int progress = 0;

        long n = read(master, buf, sizeof buf);
        if (n > 0) {
            progress = 1;
            write(1, buf, n);
            for (int i = 0; i < MAXCLIENTS; i++) if (clients[i] >= 0) {
                if (send(clients[i], buf, n, 0) < 0) { }
            }
        }

        long m = read(0, buf, sizeof buf);
        if (m > 0) { progress = 1; write(master, buf, m); }

        for (int i = 0; i < MAXCLIENTS; i++) if (clients[i] >= 0) {
            long r = recv(clients[i], buf, sizeof buf, 0);
            if (r > 0) { progress = 1; write(master, buf, r); }
            else if (r == 0) { close(clients[i]); clients[i] = -1; }
        }

        int c = accept(ls, 0, 0);
        if (c >= 0) {
            nb(c);
            int placed = 0;
            for (int i = 0; i < MAXCLIENTS; i++) if (clients[i] < 0) { clients[i] = c; placed = 1; break; }
            if (!placed) close(c);
            const char *msg = "\r\n*** a user attached to this terminal ***\r\n";
            write(master, "\n", 1);
            for (int i = 0; i < MAXCLIENTS; i++) if (clients[i] >= 0) send(clients[i], msg, strlen(msg), 0);
            write(1, msg, strlen(msg));
        }

        int st;
        if (waitpid(pid, &st, WNOHANG) == pid) done = 1;
        if (!progress) usleep(3000);
    }

    restore_console();
    close(ls);
    for (int i = 0; i < MAXCLIENTS; i++) if (clients[i] >= 0) close(clients[i]);
    printf("\r\nwterm: session ended\r\n");
    return 0;
}

static int attach_mode(const char *host, int port) {
    in_addr_t ip = inet_resolve(host);
    if (ip == 0xffffffffu) { printf("wterm: cannot resolve %s\n", host); return 1; }
    int s = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in sa; memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET; sa.sin_port = htons((uint16_t)port); sa.sin_addr.s_addr = ip;
    if (connect(s, (struct sockaddr *)&sa, sizeof sa) < 0) { printf("wterm: connect failed\n"); return 1; }
    printf("wterm: attached to %s (Ctrl-] to detach)\n", host);

    nb(s);
    raw_console();
    uint8_t buf[4096];
    int done = 0;
    while (!done) {
        int progress = 0;
        long n = recv(s, buf, sizeof buf, 0);
        if (n > 0) { progress = 1; write(1, buf, n); }
        else if (n == 0) break;

        long m = read(0, buf, sizeof buf);
        if (m > 0) {
            for (long i = 0; i < m; i++) if (buf[i] == 0x1d) { done = 1; }
            progress = 1;
            send(s, buf, m, 0);
        }
        if (!progress) usleep(3000);
    }
    restore_console();
    close(s);
    printf("\r\nwterm: detached\r\n");
    return 0;
}

int main(int argc, char **argv) {
    int port = WPORT;
    const char *host = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-p") && i + 1 < argc) port = atoi(argv[++i]);
        else host = argv[i];
    }
    if (host) return attach_mode(host, port);
    return host_mode(port);
}
