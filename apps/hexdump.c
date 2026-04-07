#include "../apps/cervus_user.h"

static const char hx[] = "0123456789abcdef";

static void print_hex8(uint8_t v) {
    char b[2]; b[0] = hx[v >> 4]; b[1] = hx[v & 0xF];
    write(1, b, 2);
}

static void print_hex64_raw(uint64_t v) {
    char b[16];
    for (int i = 15; i >= 0; i--) { b[i] = hx[v & 0xF]; v >>= 4; }
    write(1, b, 16);
}

CERVUS_MAIN(hexdump_main) {
    const char *cwd = get_cwd_flag(argc, argv);
    const char *path = NULL;
    for (int i = 1; i < argc; i++) {
        if (is_shell_flag(argv[i])) continue;
        path = argv[i]; break;
    }
    if (!path) { ws("Usage: hexdump <file>\n"); exit(1); }

    char resolved[512];
    resolve_path(cwd, path, resolved, sizeof(resolved));

    int fd = open(resolved, O_RDONLY, 0);
    if (fd < 0) {
        ws("hexdump: cannot open: "); ws(path); wn();
        exit(1);
    }

    uint8_t buf[16];
    uint64_t offset = 0;
    ssize_t n;
    while ((n = read(fd, buf, 16)) > 0) {
        print_hex64_raw(offset);
        ws("  ");
        for (int i = 0; i < 8; i++) {
            if (i < n) { print_hex8(buf[i]); wc(' '); }
            else ws("   ");
        }
        wc(' ');
        for (int i = 8; i < 16; i++) {
            if (i < n) { print_hex8(buf[i]); wc(' '); }
            else ws("   ");
        }
        ws(" |");
        for (int i = 0; i < n; i++) {
            char c = isprint(buf[i]) ? (char)buf[i] : '.';
            wc(c);
        }
        ws("|\n");
        offset += n;
    }
    print_hex64_raw(offset); wn();
    close(fd);
    exit(0);
}