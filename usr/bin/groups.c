#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <pwutil.h>

static int is_sudoer(uint32_t uid)
{
    if (uid == 0) return 1;
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

int main(int argc, char **argv)
{
    uint32_t uid;
    char name[64];

    if (argc >= 2) {
        if (pw_lookup_name(argv[1], &uid, NULL, NULL, 0, NULL, 0) != 0) {
            fprintf(stderr, "groups: '%s': no such user\n", argv[1]);
            return 1;
        }
        strncpy(name, argv[1], sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';
    } else {
        uid = (uint32_t)getuid();
        if (pw_lookup_uid(uid, name, sizeof(name), NULL, 0, NULL, 0) != 0)
            snprintf(name, sizeof(name), "%u", uid);
    }

    printf("%s", name);
    if (is_sudoer(uid)) printf(" sudo");
    printf("\n");
    return 0;
}
