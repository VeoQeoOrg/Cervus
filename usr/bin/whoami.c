#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>

int main(void) {
    int uid = (int)getuid();
    char buf[2048];
    int fd = open("/etc/passwd", O_RDONLY);
    if (fd >= 0) {
        int n = (int)read(fd, buf, sizeof(buf) - 1);
        close(fd);
        if (n > 0) {
            buf[n] = 0;
            char *line = buf;
            while (line && *line) {
                char *nl = line; while (*nl && *nl != '\n') nl++;
                char save = *nl; *nl = 0;
                char name[64] = {0};
                int i = 0; char *p = line;
                while (*p && *p != ':' && i < 63) name[i++] = *p++;
                name[i] = 0;
                if (*p == ':') {
                    p++; while (*p && *p != ':') p++;
                    if (*p == ':') {
                        p++;
                        int u = 0; while (*p >= '0' && *p <= '9') u = u*10 + (*p++ - '0');
                        if (u == uid) { printf("%s\n", name); return 0; }
                    }
                }
                *nl = save;
                line = (*nl) ? nl + 1 : nl;
            }
        }
    }
    printf("uid=%d\n", uid);
    return 0;
}
