#include <pwutil.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>

int pw_getpass(const char *prompt, char *buf, int cap) {
    if (prompt) { write(1, prompt, strlen(prompt)); }
    struct termios old, raw;
    int have = (tcgetattr(0, &old) == 0);
    if (have) {
        raw = old;
        raw.c_lflag &= ~(ECHO | ICANON);
        tcsetattr(0, TCSAFLUSH, &raw);
    }
    int len = 0;
    for (;;) {
        char c;
        int n = (int)read(0, &c, 1);
        if (n <= 0) { if (len == 0) { if (have) tcsetattr(0, TCSAFLUSH, &old); write(1,"\n",1); return -1; } break; }
        if (c == '\n' || c == '\r') break;
        if (c == 8 || c == 127) { if (len > 0) { len--; write(1, "\b \b", 3); } continue; }
        if (c == 27) {
            char e;
            if (read(0, &e, 1) <= 0) continue;
            if (e == '[' || e == 'O')
                do { if (read(0, &e, 1) <= 0) break; } while ((e >= '0' && e <= '9') || e == ';');
            continue;
        }
        if ((unsigned char)c < 32) continue;
        if (len < cap - 1) { buf[len++] = c; write(1, "*", 1); }
    }
    buf[len] = 0;
    if (have) tcsetattr(0, TCSAFLUSH, &old);
    write(1, "\n", 1);
    return len;
}

static int field(char **pp, char *out, int cap) {
    char *p = *pp;
    int i = 0;
    while (*p && *p != ':' && *p != '\n') { if (out && i < cap - 1) out[i++] = *p; p++; }
    if (out) out[i] = 0;
    if (*p == ':') p++;
    *pp = p;
    return i;
}

static int scan_passwd(const char *want_name, uint32_t want_uid, int by_name,
                       char *name, int name_cap, uint32_t *uid, uint32_t *gid,
                       char *home, int home_cap, char *shell, int shell_cap) {
    int fd = open("/etc/passwd", O_RDONLY);
    if (fd < 0) return -1;
    static char buf[4096];
    int n = (int)read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return -1;
    buf[n] = 0;

    char *line = buf;
    while (*line) {
        char *nl = line; while (*nl && *nl != '\n') nl++;
        char save = *nl; *nl = 0;
        char *p = line;
        char nm[64]; field(&p, nm, sizeof(nm));
        field(&p, NULL, 0);
        char uids[16]; field(&p, uids, sizeof(uids));
        char gids[16]; field(&p, gids, sizeof(gids));
        field(&p, NULL, 0);
        char hm[128]; field(&p, hm, sizeof(hm));
        char sh[128]; field(&p, sh, sizeof(sh));
        uint32_t u = 0; for (char *q = uids; *q >= '0' && *q <= '9'; q++) u = u*10 + (*q - '0');
        uint32_t g = 0; for (char *q = gids; *q >= '0' && *q <= '9'; q++) g = g*10 + (*q - '0');

        int match = by_name ? (strcmp(nm, want_name) == 0) : (u == want_uid);
        if (match) {
            if (name)  { strncpy(name, nm, name_cap); name[name_cap-1]=0; }
            if (uid)   *uid = u;
            if (gid)   *gid = g;
            if (home)  { strncpy(home, hm, home_cap); home[home_cap-1]=0; }
            if (shell) { strncpy(shell, sh, shell_cap); shell[shell_cap-1]=0; }
            return 0;
        }
        *nl = save;
        line = (*nl) ? nl + 1 : nl;
    }
    return -1;
}

int pw_lookup_name(const char *name, uint32_t *uid, uint32_t *gid,
                   char *home, int home_cap, char *shell, int shell_cap) {
    return scan_passwd(name, 0, 1, NULL, 0, uid, gid, home, home_cap, shell, shell_cap);
}

int pw_lookup_uid(uint32_t uid, char *name, int name_cap,
                  char *home, int home_cap, char *shell, int shell_cap) {
    return scan_passwd(NULL, uid, 0, name, name_cap, NULL, NULL, home, home_cap, shell, shell_cap);
}
