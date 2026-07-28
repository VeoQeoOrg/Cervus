#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <pwutil.h>
#include <sys/stat.h>
#include <dirent.h>

static int rewrite_filtered(const char *path, const char *name, uint32_t uid, int passwd_fmt)
{
    int fd = open(path, O_RDONLY, 0);
    if (fd < 0) return -1;
    static char in[16384], out[16384];
    int n = (int)read(fd, in, sizeof(in) - 1);
    close(fd);
    if (n < 0) return -1;
    in[n] = '\0';

    int ol = 0;
    char *line = in;
    while (*line) {
        char *nl = line;
        while (*nl && *nl != '\n') nl++;
        int keep = 1;
        if (passwd_fmt) {
            char *colon = strchr(line, ':');
            int flen = colon ? (int)(colon - line) : (int)(nl - line);
            if ((int)strlen(name) == flen && strncmp(line, name, flen) == 0) keep = 0;
        } else {
            uint32_t v = 0;
            char *q = line;
            int got = 0;
            while (*q >= '0' && *q <= '9') { v = v * 10 + (uint32_t)(*q - '0'); q++; got = 1; }
            if (got && v == uid) keep = 0;
        }
        if (keep) {
            int llen = (int)(nl - line) + (*nl == '\n' ? 1 : 0);
            if (ol + llen < (int)sizeof(out)) { memcpy(out + ol, line, llen); ol += llen; }
        }
        line = (*nl == '\n') ? nl + 1 : nl;
    }

    int wf = open(path, O_WRONLY | O_TRUNC, 0);
    if (wf < 0) return -1;
    write(wf, out, (size_t)ol);
    close(wf);
    return 0;
}

static void rmtree(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) return;
    if (st.st_type == DT_DIR) {
        DIR *d = opendir(path);
        if (d) {
            struct dirent *e;
            while ((e = readdir(d)) != NULL) {
                if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
                char c[512];
                snprintf(c, sizeof(c), "%s/%s", path, e->d_name);
                rmtree(c);
            }
            closedir(d);
        }
        rmdir(path);
    } else {
        unlink(path);
    }
}

int main(int argc, char **argv)
{
    if (getuid() != 0) { fprintf(stderr, "userdel: only root can delete users\n"); return 1; }

    int rmhome = 0;
    const char *name = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-r")) rmhome = 1;
        else if (argv[i][0] != '-') name = argv[i];
    }
    if (!name) { fprintf(stderr, "usage: userdel [-r] <name>\n"); return 1; }

    uint32_t uid = 0, gid = 0;
    char home[128] = {0};
    if (pw_lookup_name(name, &uid, &gid, home, sizeof(home), NULL, 0) != 0) {
        fprintf(stderr, "userdel: user '%s' does not exist\n", name);
        return 1;
    }
    if (uid == 0) { fprintf(stderr, "userdel: refusing to delete root\n"); return 1; }

    rewrite_filtered("/etc/passwd", name, uid, 1);
    rewrite_filtered("/etc/shadow", name, uid, 0);
    rewrite_filtered("/etc/sudoers", name, uid, 0);

    if (rmhome && home[0] && strcmp(home, "/") != 0)
        rmtree(home);

    printf("user '%s' deleted%s\n", name, rmhome ? " (home removed)" : "");
    return 0;
}
