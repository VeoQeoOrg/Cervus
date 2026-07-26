#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <pwutil.h>
#include <sys/stat.h>
#include <sys/syscall.h>

static void copy_skel(const char *home, uint32_t uid, uint32_t gid) {
    DIR *d = opendir("/etc/skel");
    if (!d) return;
    struct dirent *e;
    char sp[256], dp[256], buf[4096];
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        snprintf(sp, sizeof(sp), "/etc/skel/%s", e->d_name);
        snprintf(dp, sizeof(dp), "%s/%s", home, e->d_name);
        int in = open(sp, O_RDONLY, 0);
        if (in < 0) continue;
        int out = open(dp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (out < 0) { close(in); continue; }
        ssize_t n;
        while ((n = read(in, buf, sizeof(buf))) > 0) write(out, buf, (size_t)n);
        close(in); close(out);
        syscall3(SYS_CHOWN, (uint64_t)(uintptr_t)dp, (uint64_t)uid, (uint64_t)gid);
    }
    closedir(d);
}

static uint32_t next_free_uid(void) {
    int fd = open("/etc/passwd", O_RDONLY, 0);
    if (fd < 0) return 1000;
    char buf[8192];
    int n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return 1000;
    buf[n] = 0;
    uint32_t max = 999;
    const char *p = buf;
    while (*p) {
        const char *c1 = strchr(p, ':');
        if (c1) {
            const char *c2 = strchr(c1 + 1, ':');
            if (c2) {
                uint32_t u = (uint32_t)strtoul(c2 + 1, NULL, 10);
                if (u >= 1000 && u < 60000 && u > max) max = u;
            }
        }
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
    }
    return max + 1;
}

int main(int argc, char **argv) {
    if (getuid() != 0) { fprintf(stderr, "useradd: only root can add users\n"); return 1; }

    const char *name = NULL, *shell = "/bin/csh";
    uint32_t uid = 0;
    int sudoer = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-u") == 0 && i + 1 < argc) uid = (uint32_t)strtoul(argv[++i], NULL, 10);
        else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) shell = argv[++i];
        else if (strcmp(argv[i], "-G") == 0 && i + 1 < argc && strcmp(argv[i+1], "sudo") == 0) { sudoer = 1; i++; }
        else if (argv[i][0] != '-') name = argv[i];
    }
    if (!name) { fprintf(stderr, "usage: useradd [-u uid] [-s shell] [-G sudo] <name>\n"); return 1; }

    uint32_t eu, eg;
    if (pw_lookup_name(name, &eu, &eg, NULL, 0, NULL, 0) == 0) {
        fprintf(stderr, "useradd: user '%s' already exists\n", name);
        return 1;
    }
    if (uid == 0) uid = next_free_uid();
    uint32_t gid = uid;

    char home[128];
    snprintf(home, sizeof(home), "/home/%s", name);

    char line[256];
    int ll = snprintf(line, sizeof(line), "%s:x:%u:%u:%s:%s:%s\n", name, uid, gid, name, home, shell);
    int fd = open("/etc/passwd", O_WRONLY | O_APPEND, 0);
    if (fd < 0) { fprintf(stderr, "useradd: cannot open /etc/passwd\n"); return 1; }
    write(fd, line, ll);
    close(fd);

    mkdir("/home", 0755);
    if (mkdir(home, 0755) == 0) {
        syscall3(SYS_CHOWN, (uint64_t)(uintptr_t)home, (uint64_t)uid, (uint64_t)gid);
        copy_skel(home, uid, gid);
    }

    if (sudoer) {
        char sl[32];
        int sll = snprintf(sl, sizeof(sl), "%u\n", uid);
        int sf = open("/etc/sudoers", O_WRONLY | O_APPEND, 0);
        if (sf >= 0) { write(sf, sl, sll); close(sf); }
    }

    printf("user '%s' created (uid=%u, home=%s%s)\n", name, uid, home, sudoer ? ", sudo" : "");

    char p1[256], p2[256];
    if (pw_getpass("New password: ", p1, sizeof(p1)) < 0) return 1;
    if (pw_getpass("Retype new password: ", p2, sizeof(p2)) < 0) return 1;
    if (strcmp(p1, p2) != 0) { fprintf(stderr, "useradd: passwords do not match; user has no password\n"); return 1; }
    if (strlen(p1) < 4) { fprintf(stderr, "useradd: password too short; user has no password\n"); return 1; }

    long r = syscall2(SYS_PASSWD_SET, (uint64_t)uid, (uint64_t)(uintptr_t)p1);
    memset(p1, 0, sizeof(p1)); memset(p2, 0, sizeof(p2));
    if (r != 0) { fprintf(stderr, "useradd: failed to set password\n"); return 1; }

    printf("password set for '%s'\n", name);
    return 0;
}
