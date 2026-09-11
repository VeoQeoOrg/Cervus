#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#define CLIP_DIR   "/tmp/.clip"
#define CLIP_SLOTS 10
#define CLIP_MAX   (256 * 1024)

static const char USAGE[] =
"copy - put text on the clipboard, shared by every terminal\n"
"\n"
"Usage\n"
"  copy                 take what is typed, until Ctrl-D\n"
"  copy <file> ...      take the contents of those files\n"
"  ls -l | copy         take the output of something\n"
"  copy -l              what is on the clipboard now\n"
"  copy -c              forget everything\n"
"\n"
"What you copy in one terminal can be pasted in any other, including\n"
"over ssh into the same machine. The last ten things are kept: paste 1\n"
"is the one before last, and so on.\n";

static void slot_path(int n, char *out, size_t cap) {
    snprintf(out, cap, "%s/%d", CLIP_DIR, n);
}

static int ensure_dir(void) {
    struct stat st;
    if (stat(CLIP_DIR, &st) == 0) return 0;
    return mkdir(CLIP_DIR, 0700);
}

static void shift_history(void) {
    char from[64], to[64];
    for (int i = CLIP_SLOTS - 2; i >= 0; i--) {
        slot_path(i, from, sizeof from);
        slot_path(i + 1, to, sizeof to);
        struct stat st;
        if (stat(from, &st) != 0) continue;
        unlink(to);
        rename(from, to);
    }
}

static void preview(const char *buf, size_t n, char *out, size_t cap) {
    size_t o = 0;
    for (size_t i = 0; i < n && o + 1 < cap; i++) {
        char c = buf[i];
        if (c == '\n' || c == '\t' || c == '\r') {
            if (o + 3 >= cap) break;
            out[o++] = ' ';
            while (i + 1 < n && (buf[i+1]=='\n'||buf[i+1]=='\t'||buf[i+1]=='\r'||buf[i+1]==' ')) i++;
            continue;
        }
        if ((unsigned char)c < 32) continue;
        out[o++] = c;
    }
    out[o] = 0;
}

static int list_clips(void) {
    int found = 0;
    for (int i = 0; i < CLIP_SLOTS; i++) {
        char p[64];
        slot_path(i, p, sizeof p);
        int fd = open(p, O_RDONLY);
        if (fd < 0) continue;

        static char buf[4096];
        long n = read(fd, buf, sizeof buf);
        struct stat st;
        long total = (fstat(fd, &st) == 0) ? (long)st.st_size : n;
        close(fd);
        if (n < 0) continue;

        char pv[70];
        preview(buf, (size_t)n, pv, sizeof pv);

        if (i == 0) printf("\x1b[32m%2d\x1b[0m", i);
        else        printf("\x1b[90m%2d\x1b[0m", i);
        printf("  %-8ld  %.60s%s\n", total, pv, (total > 60) ? "..." : "");
        found++;
    }
    if (!found) printf("the clipboard is empty\n");
    else        printf("\npaste            the newest\npaste <n>        an older one\n");
    return 0;
}

int main(int argc, char **argv) {
    if (argc > 1 && (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help"))) {
        fputs(USAGE, stdout);
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "-l")) return list_clips();
    if (argc > 1 && !strcmp(argv[1], "-c")) {
        for (int i = 0; i < CLIP_SLOTS; i++) {
            char p[64]; slot_path(i, p, sizeof p); unlink(p);
        }
        printf("clipboard cleared\n");
        return 0;
    }

    if (ensure_dir() != 0) {
        fprintf(stderr, "copy: cannot use %s\n", CLIP_DIR);
        return 1;
    }

    static char buf[CLIP_MAX];
    size_t used = 0;

    if (argc > 1) {
        for (int a = 1; a < argc; a++) {
            int fd = open(argv[a], O_RDONLY);
            if (fd < 0) { fprintf(stderr, "copy: cannot read %s\n", argv[a]); return 1; }
            long n;
            while (used < sizeof buf &&
                   (n = read(fd, buf + used, sizeof buf - used)) > 0)
                used += (size_t)n;
            close(fd);
        }
    } else {
        long n;
        while (used < sizeof buf &&
               (n = read(0, buf + used, sizeof buf - used)) > 0)
            used += (size_t)n;
    }

    if (used == 0) {
        fprintf(stderr, "copy: nothing to copy\n");
        return 1;
    }

    shift_history();

    char p[64];
    slot_path(0, p, sizeof p);
    int fd = open(p, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) { fprintf(stderr, "copy: cannot write the clipboard\n"); return 1; }
    size_t off = 0;
    while (off < used) {
        long w = write(fd, buf + off, used - off);
        if (w <= 0) break;
        off += (size_t)w;
    }
    close(fd);

    char pv[70];
    preview(buf, used, pv, sizeof pv);
    printf("copied %zu bytes: \x1b[90m%.50s%s\x1b[0m\n",
           used, pv, used > 50 ? "..." : "");
    return 0;
}
