#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <arpa/inet.h>

int main(int argc, char **argv) {
    if (argc < 4) { printf("usage: mount9 <host> <port> <mountpoint>\n  e.g: mount9 10.0.0.1 564 /mnt\n"); return 1; }
    in_addr_t ip = inet_resolve(argv[1]);
    if (ip == 0xffffffffu) { fprintf(stderr, "mount9: cannot resolve %s\n", argv[1]); return 1; }
    int port = atoi(argv[2]);
    char path[256]; memset(path, 0, sizeof path);
    strncpy(path, argv[3], 255);

    long r = (long)syscall3(SYS_MOUNT9, (uint64_t)ntohl(ip), (uint64_t)port, (uint64_t)path);
    if (r < 0) { fprintf(stderr, "mount9: mount failed\n"); return 1; }
    printf("mounted 9p %s:%d at %s\n", argv[1], port, argv[3]);
    return 0;
}
