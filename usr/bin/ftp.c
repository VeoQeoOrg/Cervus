#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <ftp.h>
#include <pwutil.h>

static const char *base_of(const char *p) {
    const char *b = p, *s;
    for (s = p; *s; s++) if (*s == '/') b = s + 1;
    return b[0] ? b : "download";
}

static int read_line(const char *prompt, char *buf, int cap) {
    if (prompt) { write(1, prompt, strlen(prompt)); }
    int len = 0;
    while (len < cap - 1) {
        char c;
        long n = read(0, &c, 1);
        if (n <= 0) { if (len == 0) return -1; break; }
        if (c == '\n') break;
        buf[len++] = c;
    }
    buf[len] = 0;
    return len;
}

static int split(char *line, char **argv, int max) {
    int n = 0;
    char *p = line;
    while (*p && n < max) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        argv[n++] = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        if (*p) *p++ = 0;
    }
    return n;
}

static void help(void) {
    printf("commands: open host [port], user [name], ls [path], cd dir, pwd,\n"
           "          get remote [local], put local [remote], mkdir dir, rmdir dir,\n"
           "          del file, ascii, binary, close, bye/quit, help\n");
}

static int do_login(ftp_session *s, const char *user, const char *pass) {
    char ub[128], pb[128];
    if (!user) { printf("Name (anonymous): "); if (read_line(0, ub, sizeof ub) < 0) return -1; user = ub[0] ? ub : "anonymous"; }
    if (!pass && strcmp(user, "anonymous") != 0 && strcmp(user, "ftp") != 0) {
        if (pw_getpass("Password: ", pb, sizeof pb) < 0) return -1;
        pass = pb;
    }
    return ftp_login(s, user, pass);
}

int main(int argc, char **argv) {
    const char *user = 0, *pass = 0, *host = 0;
    int port = 0, verbose = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-u") && i + 1 < argc) user = argv[++i];
        else if (!strcmp(argv[i], "-p") && i + 1 < argc) pass = argv[++i];
        else if (!strcmp(argv[i], "-v")) verbose = 1;
        else if (!strncmp(argv[i], "ftp://", 6)) {
            return ftp_fetch(argv[i], 1, verbose) == 0 ? 0 : 1;
        }
        else if (!host) host = argv[i];
        else port = atoi(argv[i]);
    }

    ftp_session s; s.ctl = -1;
    int connected = 0, binary = 1;

    if (host) {
        if (ftp_connect(&s, host, port, verbose) == 0 && do_login(&s, user, pass) == 0) { ftp_type(&s, 1); connected = 1; printf("Connected to %s.\n", host); }
        else if (s.ctl >= 0) ftp_quit(&s);
    }

    char line[1024], *av[8];
    for (;;) {
        if (read_line("ftp> ", line, sizeof line) < 0) break;
        int n = split(line, av, 8);
        if (n == 0) continue;
        const char *cmd = av[0];

        if (!strcmp(cmd, "bye") || !strcmp(cmd, "quit") || !strcmp(cmd, "exit")) break;
        if (!strcmp(cmd, "help") || !strcmp(cmd, "?")) { help(); continue; }

        if (!strcmp(cmd, "open")) {
            if (connected) { printf("already connected; close first\n"); continue; }
            if (n < 2) { printf("usage: open host [port]\n"); continue; }
            int pt = n > 2 ? atoi(av[2]) : 0;
            if (ftp_connect(&s, av[1], pt, verbose) == 0 && do_login(&s, user, pass) == 0) { ftp_type(&s, binary); connected = 1; printf("Connected to %s.\n", av[1]); }
            else if (s.ctl >= 0) ftp_quit(&s);
            continue;
        }

        if (!connected) { printf("not connected (use: open host)\n"); continue; }

        if (!strcmp(cmd, "close") || !strcmp(cmd, "disconnect")) { ftp_quit(&s); connected = 0; }
        else if (!strcmp(cmd, "user")) { do_login(&s, n > 1 ? av[1] : 0, 0); }
        else if (!strcmp(cmd, "ls") || !strcmp(cmd, "dir")) { ftp_list(&s, n > 1 ? av[1] : 0, 1); }
        else if (!strcmp(cmd, "cd")) { if (n > 1) { if (ftp_cwd(&s, av[1]) == 0) printf("%s\n", s.reply); } else printf("usage: cd dir\n"); }
        else if (!strcmp(cmd, "pwd")) { char cwd[512]; if (ftp_pwd(&s, cwd, sizeof cwd) == 0) printf("%s\n", cwd); }
        else if (!strcmp(cmd, "ascii")) { binary = 0; ftp_type(&s, 0); printf("type: ascii\n"); }
        else if (!strcmp(cmd, "binary")) { binary = 1; ftp_type(&s, 1); printf("type: binary\n"); }
        else if (!strcmp(cmd, "mkdir")) { if (n > 1) { ftp_cmd(&s, "MKD", av[1]); printf("%s\n", s.reply); } }
        else if (!strcmp(cmd, "rmdir")) { if (n > 1) { ftp_cmd(&s, "RMD", av[1]); printf("%s\n", s.reply); } }
        else if (!strcmp(cmd, "del") || !strcmp(cmd, "delete")) { if (n > 1) { ftp_cmd(&s, "DELE", av[1]); printf("%s\n", s.reply); } }
        else if (!strcmp(cmd, "get")) {
            if (n < 2) { printf("usage: get remote [local]\n"); continue; }
            const char *local = n > 2 ? av[2] : base_of(av[1]);
            int fd = open(local, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd < 0) { printf("cannot create %s\n", local); continue; }
            if (ftp_retr(&s, av[1], fd) == 0) printf("got '%s' -> '%s'\n", av[1], local);
            else printf("get failed\n");
            close(fd);
        }
        else if (!strcmp(cmd, "put")) {
            if (n < 2) { printf("usage: put local [remote]\n"); continue; }
            const char *remote = n > 2 ? av[2] : base_of(av[1]);
            int fd = open(av[1], O_RDONLY);
            if (fd < 0) { printf("cannot open %s\n", av[1]); continue; }
            if (ftp_stor(&s, remote, fd) == 0) printf("put '%s' -> '%s'\n", av[1], remote);
            else printf("put failed\n");
            close(fd);
        }
        else printf("unknown command '%s' (try help)\n", cmd);
    }

    if (connected) ftp_quit(&s);
    return 0;
}
