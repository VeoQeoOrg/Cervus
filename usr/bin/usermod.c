#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <pwutil.h>

static int sudoers_has(uint32_t uid)
{
    int fd = open("/etc/sudoers", O_RDONLY, 0);
    if (fd < 0) return 0;
    char buf[4096];
    int n = (int)read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return 0;
    buf[n] = '\0';
    const char *p = buf;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\n') p++;
        uint32_t v = 0;
        int got = 0;
        const char *q = p;
        while (*q >= '0' && *q <= '9') { v = v * 10 + (uint32_t)(*q - '0'); q++; got = 1; }
        if (got && v == uid) return 1;
        while (*p && *p != '\n') p++;
    }
    return 0;
}

static int sudoers_add(uint32_t uid)
{
    if (sudoers_has(uid)) return 0;
    int fd = open("/etc/sudoers", O_WRONLY | O_APPEND, 0);
    if (fd < 0) return -1;
    char line[32];
    int n = snprintf(line, sizeof(line), "%u\n", uid);
    write(fd, line, (size_t)n);
    close(fd);
    return 0;
}

static int sudoers_remove(uint32_t uid)
{
    int fd = open("/etc/sudoers", O_RDONLY, 0);
    if (fd < 0) return -1;
    static char in[4096], out[4096];
    int n = (int)read(fd, in, sizeof(in) - 1);
    close(fd);
    if (n < 0) return -1;
    in[n] = '\0';
    int ol = 0;
    char *line = in;
    while (*line) {
        char *nl = line;
        while (*nl && *nl != '\n') nl++;
        uint32_t v = 0;
        int got = 0;
        char *q = line;
        while (*q >= '0' && *q <= '9') { v = v * 10 + (uint32_t)(*q - '0'); q++; got = 1; }
        if (!(got && v == uid)) {
            int llen = (int)(nl - line) + (*nl == '\n' ? 1 : 0);
            if (ol + llen < (int)sizeof(out)) { memcpy(out + ol, line, llen); ol += llen; }
        }
        line = (*nl == '\n') ? nl + 1 : nl;
    }
    int wf = open("/etc/sudoers", O_WRONLY | O_TRUNC, 0);
    if (wf < 0) return -1;
    write(wf, out, (size_t)ol);
    close(wf);
    return 0;
}

static int set_shell(const char *name, const char *shell)
{
    int fd = open("/etc/passwd", O_RDONLY, 0);
    if (fd < 0) return -1;
    static char in[8192], out[8192];
    int n = (int)read(fd, in, sizeof(in) - 1);
    close(fd);
    if (n < 0) return -1;
    in[n] = '\0';
    int ol = 0, changed = 0;
    char *line = in;
    while (*line) {
        char *nl = line;
        while (*nl && *nl != '\n') nl++;
        char *colon = strchr(line, ':');
        int flen = colon ? (int)(colon - line) : (int)(nl - line);
        if ((int)strlen(name) == flen && strncmp(line, name, flen) == 0) {
            char fields[7][160];
            for (int i = 0; i < 7; i++) fields[i][0] = '\0';
            int fi = 0, ci = 0;
            for (char *c = line; c < nl && fi < 7; c++) {
                if (*c == ':') { fields[fi][ci] = '\0'; fi++; ci = 0; }
                else if (ci < 159) { fields[fi][ci++] = *c; }
            }
            if (fi < 7) fields[fi][ci] = '\0';
            strncpy(fields[6], shell, sizeof(fields[6]) - 1);
            fields[6][sizeof(fields[6]) - 1] = '\0';
            ol += snprintf(out + ol, sizeof(out) - ol, "%s:%s:%s:%s:%s:%s:%s\n",
                           fields[0], fields[1], fields[2], fields[3],
                           fields[4], fields[5], fields[6]);
            changed = 1;
        } else {
            int llen = (int)(nl - line) + (*nl == '\n' ? 1 : 0);
            if (ol + llen < (int)sizeof(out)) { memcpy(out + ol, line, llen); ol += llen; }
        }
        line = (*nl == '\n') ? nl + 1 : nl;
    }
    if (!changed) return -1;
    int wf = open("/etc/passwd", O_WRONLY | O_TRUNC, 0);
    if (wf < 0) return -1;
    write(wf, out, (size_t)ol);
    close(wf);
    return 0;
}

static const char USAGE[] =
    "Usage: usermod [-aG sudo] [-rG sudo] [-s shell] <name>\n"
    "  -aG sudo   add the user to the sudoers\n"
    "  -rG sudo   remove the user from the sudoers\n"
    "  -s shell   set the login shell\n";

int main(int argc, char **argv)
{
    if (getuid() != 0) { fprintf(stderr, "usermod: only root can modify users\n"); return 1; }

    int add_sudo = 0, del_sudo = 0;
    const char *shell = NULL, *name = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-aG") && i + 1 < argc) { if (!strcmp(argv[++i], "sudo")) add_sudo = 1; }
        else if (!strcmp(argv[i], "-rG") && i + 1 < argc) { if (!strcmp(argv[++i], "sudo")) del_sudo = 1; }
        else if (!strcmp(argv[i], "-s") && i + 1 < argc) { shell = argv[++i]; }
        else if (argv[i][0] != '-') name = argv[i];
        else { fputs(USAGE, stderr); return 1; }
    }
    if (!name || (!add_sudo && !del_sudo && !shell)) { fputs(USAGE, stderr); return 1; }

    uint32_t uid = 0;
    if (pw_lookup_name(name, &uid, NULL, NULL, 0, NULL, 0) != 0) {
        fprintf(stderr, "usermod: user '%s' does not exist\n", name);
        return 1;
    }

    if (add_sudo) {
        if (sudoers_add(uid) == 0) printf("added '%s' to sudoers\n", name);
        else { fprintf(stderr, "usermod: cannot update /etc/sudoers\n"); return 1; }
    }
    if (del_sudo) {
        if (sudoers_remove(uid) == 0) printf("removed '%s' from sudoers\n", name);
        else { fprintf(stderr, "usermod: cannot update /etc/sudoers\n"); return 1; }
    }
    if (shell) {
        if (set_shell(name, shell) == 0) printf("shell for '%s' set to %s\n", name, shell);
        else { fprintf(stderr, "usermod: cannot update shell\n"); return 1; }
    }
    return 0;
}
