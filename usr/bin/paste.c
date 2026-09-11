#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <curses.h>

#define CLIP_DIR   "/tmp/.clip"
#define CLIP_SLOTS 10

static const char USAGE[] =
"paste - put back what copy took, in any terminal\n"
"\n"
"Usage\n"
"  paste                the last thing copied\n"
"  paste <n>            an earlier one; 1 is the one before last\n"
"  paste -l             list what is kept\n"
"  paste -i             choose from a list\n"
"\n"
"It writes to standard output, so it goes wherever you send it:\n"
"\n"
"  paste                     look at it\n"
"  paste > notes.txt         into a file\n"
"  paste >> notes.txt        onto the end of one\n"
"  paste | grep error        through something else\n";

static void slot_path(int n, char *out, size_t cap) {
    snprintf(out, cap, "%s/%d", CLIP_DIR, n);
}

static void preview(const char *buf, size_t n, char *out, size_t cap) {
    size_t o = 0;
    for (size_t i = 0; i < n && o + 1 < cap; i++) {
        char c = buf[i];
        if (c == '\n' || c == '\t' || c == '\r') {
            if (o + 2 >= cap) break;
            out[o++] = ' ';
            while (i + 1 < n && (buf[i+1]=='\n'||buf[i+1]=='\t'||buf[i+1]=='\r'||buf[i+1]==' ')) i++;
            continue;
        }
        if ((unsigned char)c < 32) continue;
        out[o++] = c;
    }
    out[o] = 0;
}

static int emit(int slot) {
    char p[64];
    slot_path(slot, p, sizeof p);
    int fd = open(p, O_RDONLY);
    if (fd < 0) {
        if (slot == 0) fprintf(stderr, "paste: the clipboard is empty\n");
        else           fprintf(stderr, "paste: nothing kept at %d\n", slot);
        return 1;
    }
    char buf[8192];
    long n;
    while ((n = read(fd, buf, sizeof buf)) > 0) {
        long off = 0;
        while (off < n) {
            long w = write(1, buf + off, (size_t)(n - off));
            if (w <= 0) break;
            off += w;
        }
    }
    close(fd);
    return 0;
}

static int load_slot(int i, char *pv, size_t pvcap, long *size) {
    char p[64];
    slot_path(i, p, sizeof p);
    int fd = open(p, O_RDONLY);
    if (fd < 0) return 0;
    static char buf[4096];
    long n = read(fd, buf, sizeof buf);
    struct stat st;
    *size = (fstat(fd, &st) == 0) ? (long)st.st_size : n;
    close(fd);
    if (n < 0) return 0;
    preview(buf, (size_t)n, pv, pvcap);
    return 1;
}

static int list_clips(void) {
    int found = 0;
    for (int i = 0; i < CLIP_SLOTS; i++) {
        char pv[80]; long size;
        if (!load_slot(i, pv, sizeof pv, &size)) continue;
        printf("%s%2d\x1b[0m  %-8ld  %.60s%s\n",
               i == 0 ? "\x1b[32m" : "\x1b[90m", i, size, pv, size > 60 ? "..." : "");
        found++;
    }
    if (!found) printf("the clipboard is empty\n");
    return 0;
}

static int pick_clip(void) {
    char pv[CLIP_SLOTS][80];
    long size[CLIP_SLOTS];
    int slot[CLIP_SLOTS];
    int n = 0;

    for (int i = 0; i < CLIP_SLOTS; i++) {
        if (!load_slot(i, pv[n], sizeof pv[n], &size[n])) continue;
        slot[n] = i;
        n++;
    }
    if (n == 0) { fprintf(stderr, "paste: the clipboard is empty\n"); return 1; }

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, 1);
    curs_set(0);

    int sel = 0, chosen = -1;
    for (;;) {
        int cols = getmaxx(stdscr);
        erase();
        attron(A_REVERSE);
        move(0, 0);
        for (int x = 0; x < cols; x++) addch(' ');
        mvprintw(0, 2, "clipboard - up/down choose, Enter paste, q cancel");
        attroff(A_REVERSE);

        for (int i = 0; i < n; i++) {
            if (i == sel) attron(A_REVERSE);
            mvprintw(2 + i, 2, " %2d  %-8ld  %.*s ",
                     slot[i], size[i], cols - 22, pv[i]);
            if (i == sel) attroff(A_REVERSE);
        }
        refresh();

        int ch = getch();
        if (ch == KEY_UP)        { sel = sel > 0 ? sel - 1 : n - 1; }
        else if (ch == KEY_DOWN) { sel = sel < n - 1 ? sel + 1 : 0; }
        else if (ch == '\n' || ch == '\r') { chosen = slot[sel]; break; }
        else if (ch == 'q' || ch == 'Q' || ch == 27) break;
    }

    endwin();
    if (chosen < 0) return 1;
    return emit(chosen);
}

int main(int argc, char **argv) {
    if (argc > 1 && (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help"))) {
        fputs(USAGE, stdout);
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "-l")) return list_clips();
    if (argc > 1 && !strcmp(argv[1], "-i")) return pick_clip();

    int slot = 0;
    if (argc > 1) {
        slot = atoi(argv[1]);
        if (slot < 0 || slot >= CLIP_SLOTS) {
            fprintf(stderr, "paste: keep only %d, numbered 0 to %d\n",
                    CLIP_SLOTS, CLIP_SLOTS - 1);
            return 1;
        }
    }
    return emit(slot);
}
