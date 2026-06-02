#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <errno.h>
#include <dirent.h>
#include <termios.h>
#include <sys/stat.h>
#include <sys/cervus.h>
#include <sys/syscall.h>
#include <cervus_util.h>

#define TIOCGWINSZ    0x5413
#define TIOCSNONBLOCK 0x5481

#define MBR_TYPE_FAT32_LBA  0x0C
#define MBR_TYPE_LINUX      0x83
#define MBR_TYPE_LINUX_SWAP 0x82

#define MAX_DISKS    8
#define MAX_ENTRIES  256
#define MAX_LOG_LINES 16

#define KEY_UP    1001
#define KEY_DOWN  1002
#define KEY_LEFT  1003
#define KEY_RIGHT 1004
#define KEY_ESC   27
#define KEY_ENTER 10

#define EXIT_LIVE           0
#define EXIT_CANCEL         1
#define EXIT_MOUNT_EXISTING 10

typedef struct { uint16_t ws_row, ws_col, ws_xpixel, ws_ypixel; } winsize_t;

typedef enum { DISK_KIND_INTERNAL, DISK_KIND_USB, DISK_KIND_OTHER } disk_kind_t;

typedef struct {
    char        name[32];
    char        model[41];
    uint64_t    sectors;
    uint64_t    size_bytes;
    uint32_t    sector_size;
    disk_kind_t kind;
} disk_entry_t;

typedef struct {
    uint32_t esp_mb;
    int      have_swap;
    uint32_t swap_mb;
    uint32_t total_mb;
} layout_t;

typedef struct {
    char d_name[64];
    int  d_type;
} dent_t;

static struct termios g_saved_tio;
static int g_tio_saved = 0;
static int g_nonblock_on = 0;
static int g_cols = 80;
static int g_rows = 24;

static int g_pane_left_col   = 1;
static int g_pane_left_w     = 60;
static int g_pane_right_col  = 62;
static int g_pane_right_w    = 60;
static int g_pane_split      = 1;

static char g_log[MAX_LOG_LINES][96];
static int  g_log_n = 0;
static int  g_log_total = 0;

static void compute_panes(void) {
    if (g_cols < 100) {
        g_pane_split    = 0;
        g_pane_left_col = 2;
        g_pane_left_w   = g_cols - 4;
        g_pane_right_col = 0;
        g_pane_right_w  = 0;
    } else {
        g_pane_split    = 1;
        int margin = 2;
        int gutter = 2;
        int half   = (g_cols - margin * 2 - gutter) / 2;
        g_pane_left_col  = margin + 1;
        g_pane_left_w    = half;
        g_pane_right_col = margin + 1 + half + gutter;
        g_pane_right_w   = g_cols - g_pane_right_col - margin + 1;
    }
}

static void term_size_query(void) {
    winsize_t ws;
    if (syscall3(SYS_IOCTL, 1, TIOCGWINSZ, &ws) == 0 && ws.ws_col >= 60 && ws.ws_row >= 16) {
        g_cols = ws.ws_col;
        g_rows = ws.ws_row;
    }
    compute_panes();
}

static int set_nonblock(int on) {
    int v = on;
    int r = (int)syscall3(SYS_IOCTL, 0, TIOCSNONBLOCK, &v);
    if (r == 0) g_nonblock_on = on;
    return r;
}

static void term_enter_raw(void) {
    if (tcgetattr(0, &g_saved_tio) < 0) return;
    g_tio_saved = 1;
    struct termios t = g_saved_tio;
    t.c_lflag &= ~(ICANON | ECHO | ISIG | IEXTEN);
    t.c_iflag &= ~(IXON | ICRNL | INPCK | ISTRIP);
    t.c_oflag &= ~(OPOST);
    tcsetattr(0, TCSANOW, &t);
    set_nonblock(1);
    fputs("\x1b[?25l", stdout);
    fflush(stdout);
}

static void term_restore(void) {
    if (g_nonblock_on) set_nonblock(0);
    if (g_tio_saved) tcsetattr(0, TCSANOW, &g_saved_tio);
    fputs("\x1b[?25h\x1b[0m", stdout);
    fflush(stdout);
}

static int read_byte_nb(unsigned char *out) {
    ssize_t n = read(0, out, 1);
    return (n == 1) ? 1 : 0;
}

static int read_byte_block(unsigned char *out) {
    for (;;) {
        if (read_byte_nb(out)) return 1;
        syscall1(SYS_SLEEP_NS, 5000000ULL);
    }
}

static int read_key(void) {
    unsigned char c;
    if (!read_byte_block(&c)) return -1;
    if (c == 0x1B) {
        unsigned char s1 = 0, s2 = 0;
        int got1 = 0;
        for (int i = 0; i < 40 && !got1; i++) {
            if (read_byte_nb(&s1)) { got1 = 1; break; }
            syscall1(SYS_SLEEP_NS, 1000000ULL);
        }
        if (!got1) return KEY_ESC;
        if (s1 != '[' && s1 != 'O') return KEY_ESC;
        int got2 = 0;
        for (int i = 0; i < 40 && !got2; i++) {
            if (read_byte_nb(&s2)) { got2 = 1; break; }
            syscall1(SYS_SLEEP_NS, 1000000ULL);
        }
        if (!got2) return KEY_ESC;
        switch (s2) {
            case 'A': return KEY_UP;
            case 'B': return KEY_DOWN;
            case 'C': return KEY_RIGHT;
            case 'D': return KEY_LEFT;
            default:  return KEY_ESC;
        }
    }
    if (c == '\r') return KEY_ENTER;
    return (int)c;
}

static void go_xy(int row, int col) {
    printf("\x1b[%d;%dH", row, col);
}

static void clear_screen(void) {
    fputs("\x1b[2J\x1b[H", stdout);
}

static void draw_pane_title(int row, int col, int w, const char *title) {
    if (!title || !*title) return;
    int tl = (int)strlen(title);
    int tcol = col + (w - tl) / 2;
    if (tcol < col) tcol = col;
    go_xy(row, tcol);
    fputs(C_BOLD C_CYAN, stdout);
    fputs(title, stdout);
    fputs(C_RESET, stdout);
    int left_gap  = (g_pane_split && col != g_pane_left_col) ? 1 : 0;
    int right_gap = (g_pane_split && col == g_pane_left_col) ? 2 : 0;
    go_xy(row + 1, col + left_gap);
    fputs(C_GRAY, stdout);
    for (int i = 0; i < w - left_gap - right_gap; i++) putchar('-');
    fputs(C_RESET, stdout);
}

static void draw_vsplit(void) {
    if (!g_pane_split) return;
    int col = g_pane_right_col - 2;
    for (int r = 1; r <= g_rows - 1; r++) {
        go_xy(r, col);
        fputs(C_GRAY "|" C_RESET, stdout);
    }
}

static void hide_cursor(void) {
    fflush(stdout);
    static const char seq[] = "\x1b[?25l\x1b[1;1H\x1b[?25l";
    write(1, seq, sizeof(seq) - 1);
}

static void hint_line(const char *txt) {
    go_xy(g_rows, 1);
    fputs("\x1b[K", stdout);
    fputs(C_GRAY "  ", stdout);
    fputs(txt, stdout);
    fputs(C_RESET, stdout);
}

static void format_mb(uint64_t bytes, char *out, size_t cap) {
    uint64_t mb = bytes / (1024 * 1024);
    if (mb >= 1024) {
        uint64_t gb10 = (bytes * 10) / (1024ULL * 1024 * 1024);
        snprintf(out, cap, "%llu.%llu GB", (unsigned long long)(gb10 / 10),
                                            (unsigned long long)(gb10 % 10));
    } else {
        snprintf(out, cap, "%llu MB", (unsigned long long)mb);
    }
}

static void brackets_int(char *out, size_t cap, int value, int inner_w) {
    char num[16];
    snprintf(num, sizeof(num), "%d", value);
    int nl = (int)strlen(num);
    int pad = inner_w - nl;
    int lp = pad / 2;
    int rp = pad - lp;
    if (lp < 0) lp = 0;
    if (rp < 0) rp = 0;
    snprintf(out, cap, "[%*s%s%*s]", lp, "", num, rp, "");
}

static void brackets_str(char *out, size_t cap, const char *txt, int inner_w) {
    int nl = (int)strlen(txt);
    int pad = inner_w - nl;
    int lp = pad / 2;
    int rp = pad - lp;
    if (lp < 0) lp = 0;
    if (rp < 0) rp = 0;
    snprintf(out, cap, "[%*s%s%*s]", lp, "", txt, rp, "");
}

static disk_kind_t classify_disk(const char *name) {
    if (strncmp(name, "uhd", 3) == 0) return DISK_KIND_USB;
    if (strncmp(name, "usb", 3) == 0) return DISK_KIND_USB;
    if (strncmp(name, "hd",  2) == 0) return DISK_KIND_INTERNAL;
    if (strncmp(name, "sd",  2) == 0) return DISK_KIND_INTERNAL;
    if (strncmp(name, "nvme", 4) == 0) return DISK_KIND_INTERNAL;
    if (strncmp(name, "sr",  2) == 0) return DISK_KIND_OTHER;
    return DISK_KIND_OTHER;
}

static int list_disks(disk_entry_t *out, int max) {
    int n = 0;
    for (int i = 0; i < 64 && n < max; i++) {
        cervus_disk_info_t info;
        memset(&info, 0, sizeof(info));
        int r = cervus_disk_info(i, &info);
        if (r == -ERANGE) break;
        if (r < 0) continue;
        if (!info.present || info.is_partition) continue;
        memset(&out[n], 0, sizeof(out[n]));
        size_t nlen = strlen(info.name);
        if (nlen >= sizeof(out[n].name)) nlen = sizeof(out[n].name) - 1;
        memcpy(out[n].name, info.name, nlen);
        size_t mlen = strlen(info.model);
        if (mlen >= sizeof(out[n].model)) mlen = sizeof(out[n].model) - 1;
        memcpy(out[n].model, info.model, mlen);
        out[n].sectors     = info.sectors;
        out[n].size_bytes  = info.size_bytes;
        out[n].sector_size = info.sector_size ? info.sector_size : 512;
        out[n].kind        = classify_disk(info.name);
        n++;
    }
    return n;
}

static const char *kind_tag(disk_kind_t k) {
    switch (k) {
        case DISK_KIND_USB:      return C_YELLOW "[USB]" C_RESET;
        case DISK_KIND_INTERNAL: return C_GREEN  "[internal]" C_RESET;
        default:                 return C_GRAY   "[other]" C_RESET;
    }
}

static void make_part_name(const char *disk, int n, char *out, size_t cap) {
    size_t l = strlen(disk);
    const char *sep = (l > 0 && disk[l - 1] >= '0' && disk[l - 1] <= '9') ? "p" : "";
    snprintf(out, cap, "%s%s%d", disk, sep, n);
}

typedef void (*info_fn)(int row, int col, int w);

static void info_print(int *row, int col, int w, int is_dim, const char *txt) {
    go_xy(*row, col + 2);
    if (is_dim) fputs(C_GRAY, stdout);
    int len = (int)strlen(txt);
    int max_len = w - 2;
    if (len > max_len) len = max_len;
    for (int i = 0; i < len; i++) putchar(txt[i]);
    if (is_dim) fputs(C_RESET, stdout);
    (*row)++;
}

static void info_box(const char *title, info_fn render) {
    if (!g_pane_split) return;
    int row = 2;
    int col = g_pane_right_col;
    int w   = g_pane_right_w;
    if (w < 30) return;
    draw_pane_title(row, col, w, title);
    if (render) render(row + 3, col, w);
}

static void info_mode(int row, int col, int w) {
    info_print(&row, col, w, 1, "Cervus is a 64-bit OS written from");
    info_print(&row, col, w, 1, "scratch for x86_64.");
    info_print(&row, col, w, 1, "");
    info_print(&row, col, w, 0, "Modes:");
    info_print(&row, col, w, 1, "  Automatic - picks the largest internal");
    info_print(&row, col, w, 1, "              disk and uses safe defaults.");
    info_print(&row, col, w, 1, "  Manual    - choose target disk and tune");
    info_print(&row, col, w, 1, "              ESP/swap sizes.");
    info_print(&row, col, w, 1, "  USB       - filtered to USB disks only.");
    info_print(&row, col, w, 1, "");
    info_print(&row, col, w, 0, "Filesystems:");
    info_print(&row, col, w, 1, "  ESP  - FAT32, holds kernel + Limine");
    info_print(&row, col, w, 1, "         bootloader.");
    info_print(&row, col, w, 1, "  root - ext2, the live filesystem.");
    info_print(&row, col, w, 1, "  swap - reserved area, not yet used.");
}

static void info_boot(int row, int col, int w) {
    info_print(&row, col, w, 1, "A disk has been detected.");
    info_print(&row, col, w, 1, "");
    info_print(&row, col, w, 0, "Options:");
    info_print(&row, col, w, 1, "  Live          - keep running from the");
    info_print(&row, col, w, 1, "                  ISO, disk is untouched.");
    info_print(&row, col, w, 1, "  Install       - run the installer to");
    info_print(&row, col, w, 1, "                  set up Cervus.");
    info_print(&row, col, w, 1, "  Use installed - mount the existing");
    info_print(&row, col, w, 1, "                  install at /mnt so files");
    info_print(&row, col, w, 1, "                  there persist.");
    info_print(&row, col, w, 1, "");
    info_print(&row, col, w, 0, "Nothing happens to your disks until you");
    info_print(&row, col, w, 0, "confirm the installation.");
}

static void info_disk(int row, int col, int w) {
    info_print(&row, col, w, 1, "Select a target disk.");
    info_print(&row, col, w, 1, "");
    info_print(&row, col, w, 0, "Tags:");
    info_print(&row, col, w, 1, "  [internal] - SATA/AHCI/NVMe/IDE");
    info_print(&row, col, w, 1, "  [USB]      - USB flash drives");
    info_print(&row, col, w, 1, "  [other]    - CD-ROM and similar");
    info_print(&row, col, w, 1, "");
    info_print(&row, col, w, 0, "Tip:");
    info_print(&row, col, w, 1, "  USB sticks are perfect for portable");
    info_print(&row, col, w, 1, "  installs you can carry between PCs.");
    info_print(&row, col, w, 1, "");
    info_print(&row, col, w, 0, "Warning:");
    info_print(&row, col, w, 1, "  All data on the chosen disk will be");
    info_print(&row, col, w, 1, "  erased during install.");
}

static void info_layout(int row, int col, int w) {
    info_print(&row, col, w, 1, "Customize the partition layout.");
    info_print(&row, col, w, 1, "");
    info_print(&row, col, w, 0, "ESP (FAT32):");
    info_print(&row, col, w, 1, "  Holds kernel + Limine bootloader.");
    info_print(&row, col, w, 1, "  64 MB is plenty; min 16 MB.");
    info_print(&row, col, w, 1, "");
    info_print(&row, col, w, 0, "Swap:");
    info_print(&row, col, w, 1, "  Reserved space, currently unused.");
    info_print(&row, col, w, 1, "");
    info_print(&row, col, w, 0, "Root (ext2):");
    info_print(&row, col, w, 1, "  Gets the rest of the disk.");
    info_print(&row, col, w, 1, "  Holds /bin, /apps, /usr, /home.");
    info_print(&row, col, w, 1, "");
    info_print(&row, col, w, 0, "Keys:");
    info_print(&row, col, w, 1, "  Up/Down  - move between fields");
    info_print(&row, col, w, 1, "  Enter    - edit field / start install");
    info_print(&row, col, w, 1, "  Left/Rt  - decrement / increment");
    info_print(&row, col, w, 1, "  Digits   - type a value directly");
}

static void info_confirm(int row, int col, int w) {
    info_print(&row, col, w, 1, "Review the layout carefully.");
    info_print(&row, col, w, 1, "");
    info_print(&row, col, w, 0, "What happens next:");
    info_print(&row, col, w, 1, "  1. Write MBR partition table");
    info_print(&row, col, w, 1, "  2. Format ESP as FAT32");
    info_print(&row, col, w, 1, "  3. Format root as ext2");
    info_print(&row, col, w, 1, "  4. Mount partitions");
    info_print(&row, col, w, 1, "  5. Copy kernel + bootloader to ESP");
    info_print(&row, col, w, 1, "  6. Populate root with /bin etc.");
    info_print(&row, col, w, 1, "  7. Write limine.conf");
    info_print(&row, col, w, 1, "  8. Install BIOS stage1 to MBR");
    info_print(&row, col, w, 1, "");
    info_print(&row, col, w, 0, "Cancel here is still safe:");
    info_print(&row, col, w, 1, "  No bytes are written until you say");
    info_print(&row, col, w, 1, "  yes.");
}

static void info_progress(int row, int col, int w) {
    info_print(&row, col, w, 1, "Installation is running.");
    info_print(&row, col, w, 1, "");
    info_print(&row, col, w, 0, "Do not power off the machine.");
    info_print(&row, col, w, 1, "");
    info_print(&row, col, w, 1, "Each step is shown on the left.");
    info_print(&row, col, w, 1, "Steps marked OK have completed.");
    info_print(&row, col, w, 1, "On any failure the installer stops");
    info_print(&row, col, w, 1, "and unmounts the partitions.");
    info_print(&row, col, w, 1, "");
    info_print(&row, col, w, 0, "Large copies (root filesystem) can");
    info_print(&row, col, w, 0, "take a minute or two on slow USB.");
}

static void render_menu_item(int row, int col, int item_w, int selected,
                             const char *text)
{
    go_xy(row, col);
    if (selected) {
        fputs("\x1b[7m", stdout);
        int written = 2 + (int)strlen(text);
        fputs("> ", stdout);
        fputs(text, stdout);
        while (written < item_w) { putchar(' '); written++; }
        fputs("\x1b[0m", stdout);
    } else {
        fputs("  ", stdout);
        fputs(text, stdout);
    }
}

static int menu_in_box(const char *title, const char *items[], int n_items,
                       int initial, info_fn info)
{
    int sel = initial;
    if (sel < 0) sel = 0;
    if (sel >= n_items) sel = n_items - 1;

    int title_row = 2;
    int item_start_row = 5;
    int pane_col = g_pane_left_col;
    int pane_w   = g_pane_left_w;
    int item_w   = pane_w - 2;

    for (;;) {
        hide_cursor();
        clear_screen();
        draw_pane_title(title_row, pane_col, pane_w, title);
        for (int i = 0; i < n_items; i++) {
            render_menu_item(item_start_row + i, pane_col, item_w, i == sel, items[i]);
        }
        draw_vsplit();
        info_box("Help", info);
        hint_line("Up/Down navigate   Enter select   Esc cancel");
        hide_cursor();
        fflush(stdout);

        int k = read_key();
        if (k == KEY_UP)        { if (sel > 0) sel--; else sel = n_items - 1; }
        else if (k == KEY_DOWN) { if (sel + 1 < n_items) sel++; else sel = 0; }
        else if (k == KEY_ENTER || k == ' ') return sel;
        else if (k == KEY_ESC || k == 'q' || k == 'Q') return -1;
    }
}

static int disk_select_in_box(const char *title, disk_entry_t *disks, int n_disks,
                              int initial, info_fn info)
{
    int sel = initial;
    if (sel < 0) sel = 0;
    if (sel >= n_disks) sel = n_disks - 1;

    int title_row = 2;
    int item_start_row = 5;
    int pane_col = g_pane_left_col;
    int pane_w   = g_pane_left_w;
    int item_w   = pane_w - 2;

    for (;;) {
        hide_cursor();
        clear_screen();
        draw_pane_title(title_row, pane_col, pane_w, title);
        for (int i = 0; i < n_disks; i++) {
            char sizebuf[20];
            format_mb(disks[i].size_bytes, sizebuf, sizeof(sizebuf));
            char line[96];
            int n = snprintf(line, sizeof(line),
                             "%-8s %9s  %-18.18s ",
                             disks[i].name, sizebuf, disks[i].model);
            go_xy(item_start_row + i, pane_col);
            if (i == sel) {
                fputs("\x1b[7m", stdout);
                fputs("> ", stdout);
                fputs(line, stdout);
                const char *t = kind_tag(disks[i].kind);
                int tag_visible = 0;
                if (disks[i].kind == DISK_KIND_USB)      tag_visible = 5;
                else if (disks[i].kind == DISK_KIND_INTERNAL) tag_visible = 10;
                else tag_visible = 7;
                fputs(t, stdout);
                int written = 2 + n + tag_visible;
                while (written < item_w) { putchar(' '); written++; }
                fputs("\x1b[0m", stdout);
            } else {
                fputs("  ", stdout);
                fputs(line, stdout);
                fputs(kind_tag(disks[i].kind), stdout);
            }
        }
        draw_vsplit();
        info_box("Help", info);
        hint_line("Up/Down navigate   Enter select   Esc back");
        hide_cursor();
        fflush(stdout);

        int k = read_key();
        if (k == KEY_UP)        { if (sel > 0) sel--; else sel = n_disks - 1; }
        else if (k == KEY_DOWN) { if (sel + 1 < n_disks) sel++; else sel = 0; }
        else if (k == KEY_ENTER || k == ' ') return sel;
        else if (k == KEY_ESC || k == 'q' || k == 'Q') return -1;
    }
}

static int int_edit(int row, int col, int value, int min_v, int max_v) {
    char buf[20];
    for (;;) {
        brackets_int(buf, sizeof(buf), value, 8);
        go_xy(row, col);
        fputs(C_BOLD C_YELLOW, stdout);
        fputs(buf, stdout);
        fputs(C_RESET, stdout);
        fflush(stdout);
        int k = read_key();
        if (k == KEY_LEFT || k == KEY_DOWN || k == '-') {
            if (value > min_v) {
                int step = (value > 1024) ? 64 : (value > 128) ? 16 : 1;
                value -= step;
                if (value < min_v) value = min_v;
            }
        } else if (k == KEY_RIGHT || k == KEY_UP || k == '+' || k == '=') {
            if (value < max_v) {
                int step = (value >= 1024) ? 64 : (value >= 128) ? 16 : 1;
                value += step;
                if (value > max_v) value = max_v;
            }
        } else if (k == KEY_ENTER || k == '\t' || k == KEY_ESC) {
            break;
        } else if (k >= '0' && k <= '9') {
            int v = k - '0';
            for (;;) {
                brackets_int(buf, sizeof(buf), v, 8);
                go_xy(row, col);
                fputs(C_BOLD C_YELLOW, stdout);
                fputs(buf, stdout);
                fputs(C_RESET, stdout);
                fflush(stdout);
                int k2 = read_key();
                if (k2 >= '0' && k2 <= '9') {
                    int nv = v * 10 + (k2 - '0');
                    if (nv > max_v) break;
                    v = nv;
                } else if (k2 == KEY_ENTER || k2 == '\t' || k2 == KEY_ESC) {
                    value = (v < min_v) ? min_v : v;
                    goto done;
                } else if (k2 == 8 || k2 == 127) {
                    v /= 10;
                } else {
                    break;
                }
            }
            value = (v < min_v) ? min_v : v;
        }
    }
done:
    brackets_int(buf, sizeof(buf), value, 8);
    go_xy(row, col);
    fputs(buf, stdout);
    fflush(stdout);
    return value;
}

static int bool_edit(int row, int col, int value) {
    char buf[16];
    for (;;) {
        brackets_str(buf, sizeof(buf), value ? "Yes" : "No", 5);
        go_xy(row, col);
        fputs(C_BOLD C_YELLOW, stdout);
        fputs(buf, stdout);
        fputs(C_RESET, stdout);
        fflush(stdout);
        int k = read_key();
        if (k == KEY_LEFT || k == KEY_RIGHT || k == KEY_UP || k == KEY_DOWN ||
            k == ' ' || k == 'y' || k == 'Y' || k == 'n' || k == 'N')
        {
            value = !value;
        } else if (k == KEY_ENTER || k == '\t' || k == KEY_ESC) {
            break;
        }
    }
    brackets_str(buf, sizeof(buf), value ? "Yes" : "No", 5);
    go_xy(row, col);
    fputs(buf, stdout);
    fflush(stdout);
    return value;
}

static int manual_layout(const disk_entry_t *d, layout_t *L) {
    uint32_t total_mb = (uint32_t)(d->size_bytes / (1024 * 1024));
    if (total_mb < 80) {
        hide_cursor();
        clear_screen();
        go_xy(g_rows / 2, g_pane_left_col);
        fputs(C_RED "  Disk too small for install" C_RESET, stdout);
        fflush(stdout);
        read_key();
        return -1;
    }
    L->total_mb  = total_mb;
    if (L->esp_mb == 0)  L->esp_mb  = (total_mb < 256) ? 32 : 64;
    if (L->swap_mb == 0) L->swap_mb = 16;

    int title_row = 2;
    int content_row = 5;
    int pane_col = g_pane_left_col;
    int pane_w   = g_pane_left_w;
    int item_w   = pane_w - 2;
    int focus = 0;

    for (;;) {
        hide_cursor();
        clear_screen();
        char title[64];
        snprintf(title, sizeof(title), "Layout - %s (%u MB)", d->name, total_mb);
        draw_pane_title(title_row, pane_col, pane_w, title);

        uint32_t used = L->esp_mb + (L->have_swap ? L->swap_mb : 0);
        uint32_t root_mb = (total_mb > used + 8) ? (total_mb - used - 8) : 0;

        go_xy(content_row + 0, pane_col);  fputs("  ESP size (FAT32):", stdout);
        go_xy(content_row + 1, pane_col);  fputs("  Use swap:        ", stdout);
        go_xy(content_row + 2, pane_col);  fputs("  Swap size:       ", stdout);
        go_xy(content_row + 3, pane_col);
        fputs("  Root (ext2):     ", stdout);
        printf(C_GRAY "%u MB" C_RESET, root_mb);

        go_xy(content_row + 5, pane_col);
        printf(C_GRAY "  Used: %u MB / %u MB" C_RESET, used, total_mb);

        int field_col = pane_col + 22;
        char buf[20];

        brackets_int(buf, sizeof(buf), (int)L->esp_mb, 8);
        go_xy(content_row + 0, field_col);
        if (focus == 0) fputs(C_BOLD C_YELLOW, stdout); else fputs(C_GRAY, stdout);
        fputs(buf, stdout);
        fputs(" MB" C_RESET, stdout);

        brackets_str(buf, sizeof(buf), L->have_swap ? "Yes" : "No", 5);
        go_xy(content_row + 1, field_col);
        if (focus == 1) fputs(C_BOLD C_YELLOW, stdout); else fputs(C_GRAY, stdout);
        fputs(buf, stdout);
        fputs(C_RESET, stdout);

        go_xy(content_row + 2, field_col);
        if (!L->have_swap) {
            brackets_str(buf, sizeof(buf), "---", 8);
            fputs(C_GRAY, stdout);
            fputs(buf, stdout);
            fputs(" MB" C_RESET, stdout);
        } else {
            brackets_int(buf, sizeof(buf), (int)L->swap_mb, 8);
            if (focus == 2) fputs(C_BOLD C_YELLOW, stdout); else fputs(C_GRAY, stdout);
            fputs(buf, stdout);
            fputs(" MB" C_RESET, stdout);
        }

        const char *actions[2] = { "Start installation", "Back" };
        for (int i = 0; i < 2; i++) {
            render_menu_item(content_row + 8 + i, pane_col, item_w,
                             focus == 3 + i, actions[i]);
        }

        draw_vsplit();
        info_box("Help", info_layout);
        hint_line("Up/Down move   Enter edit/confirm   Left/Right or digits   Esc back");
        hide_cursor();
        fflush(stdout);

        int k = read_key();
        if (k == KEY_UP) {
            focus--;
            if (focus < 0) focus = 4;
            if (focus == 2 && !L->have_swap) focus = 1;
        } else if (k == KEY_DOWN || k == '\t') {
            focus++;
            if (focus > 4) focus = 0;
            if (focus == 2 && !L->have_swap) focus = 3;
        } else if (k == KEY_ESC) {
            return -1;
        } else if (k == KEY_ENTER) {
            if (focus == 0) {
                uint32_t max_esp = (total_mb > 32 + 16) ? (total_mb - 32) : 32;
                if (max_esp > 1024) max_esp = 1024;
                L->esp_mb = (uint32_t)int_edit(content_row + 0, field_col,
                                               (int)L->esp_mb, 16, (int)max_esp);
            } else if (focus == 1) {
                L->have_swap = bool_edit(content_row + 1, field_col, L->have_swap);
            } else if (focus == 2 && L->have_swap) {
                uint32_t max_swap = (total_mb > L->esp_mb + 32)
                                    ? (total_mb - L->esp_mb - 32) : 16;
                if (max_swap > 2048) max_swap = 2048;
                L->swap_mb = (uint32_t)int_edit(content_row + 2, field_col,
                                                (int)L->swap_mb, 8, (int)max_swap);
            } else if (focus == 3) {
                if (root_mb < 32) {
                    hint_line(C_RED "Root partition too small - adjust ESP/swap" C_RESET);
                    fflush(stdout);
                    read_key();
                    continue;
                }
                return 0;
            } else if (focus == 4) {
                return -1;
            }
        }
    }
}

static int confirm_screen(const disk_entry_t *d, const layout_t *L) {
    uint32_t esp_mb  = L->esp_mb;
    uint32_t swap_mb = L->have_swap ? L->swap_mb : 0;
    uint64_t total_mb_64 = d->size_bytes / (1024 * 1024);
    uint32_t total_mb = (total_mb_64 > 0xFFFFFFFFULL) ? 0xFFFFFFFFu : (uint32_t)total_mb_64;
    uint32_t root_mb = (total_mb > esp_mb + swap_mb + 8) ? (total_mb - esp_mb - swap_mb - 8) : 0;

    int title_row = 2;
    int content_row = 5;
    int pane_col = g_pane_left_col;
    int pane_w   = g_pane_left_w;
    int item_w   = pane_w - 2;
    int sel = 0;

    char part1[40], part2[40], part3[40];
    make_part_name(d->name, 1, part1, sizeof(part1));
    make_part_name(d->name, 2, part2, sizeof(part2));
    make_part_name(d->name, 3, part3, sizeof(part3));

    for (;;) {
        hide_cursor();
        clear_screen();
        draw_pane_title(title_row, pane_col, pane_w, "Confirm Installation");

        go_xy(content_row + 0, pane_col);
        char sizebuf[20];
        format_mb(d->size_bytes, sizebuf, sizeof(sizebuf));
        printf("  Target: " C_BOLD "%s" C_RESET " (%s)", d->name, sizebuf);

        go_xy(content_row + 1, pane_col);
        printf(C_GRAY "  %.34s" C_RESET, d->model);

        go_xy(content_row + 3, pane_col);
        fputs(C_CYAN "  Partition layout:" C_RESET, stdout);

        go_xy(content_row + 4, pane_col + 2);
        printf("/dev/%-10s %5u MB  FAT32  (ESP)", part1, esp_mb);
        go_xy(content_row + 5, pane_col + 2);
        printf("/dev/%-10s %5u MB  ext2   (root)", part2, root_mb);
        if (L->have_swap) {
            go_xy(content_row + 6, pane_col + 2);
            printf("/dev/%-10s %5u MB  swap", part3, swap_mb);
        }

        go_xy(content_row + 8, pane_col);
        fputs(C_RED "  ALL DATA on " C_RESET, stdout);
        fputs(C_BOLD, stdout);
        fputs(d->name, stdout);
        fputs(C_RESET, stdout);
        fputs(C_RED " will be ERASED!" C_RESET, stdout);

        const char *acts[2] = { "Cancel", "Yes, install" };
        for (int i = 0; i < 2; i++) {
            render_menu_item(content_row + 10 + i, pane_col, item_w,
                             i == sel, acts[i]);
        }

        draw_vsplit();
        info_box("What will happen", info_confirm);
        hint_line("Up/Down navigate   Enter confirm   Esc back");
        hide_cursor();
        fflush(stdout);

        int k = read_key();
        if (k == KEY_UP || k == KEY_DOWN) sel = !sel;
        else if (k == KEY_ENTER) return sel;
        else if (k == KEY_ESC)   return 0;
    }
}

static int g_step_title_row = 2;
static int g_step_content_row = 5;
static int g_step_pane_col = 1;
static int g_step_pane_w   = 60;
static int g_step_rows_avail = 12;
static int g_step_n       = 0;
static int g_step_total   = 0;

static void install_screen_init(int total_steps) {
    g_step_title_row   = 2;
    g_step_content_row = 5;
    g_step_pane_col    = g_pane_left_col;
    g_step_pane_w      = g_pane_left_w;
    g_step_rows_avail  = g_rows - g_step_content_row - 2;
    if (g_step_rows_avail < 4) g_step_rows_avail = 4;
    g_step_n       = 0;
    g_step_total   = total_steps;
    g_log_n        = 0;
    g_log_total    = 0;
    for (int i = 0; i < MAX_LOG_LINES; i++) g_log[i][0] = 0;
    hide_cursor();
    clear_screen();
    draw_pane_title(g_step_title_row, g_step_pane_col, g_step_pane_w,
                    "Installing Cervus");
    draw_vsplit();
    info_box("Help", info_progress);
    hint_line("Installation in progress - do not power off");
    hide_cursor();
    fflush(stdout);
}

static void log_append(const char *line) {
    int slot = g_log_total % MAX_LOG_LINES;
    snprintf(g_log[slot], sizeof(g_log[slot]), "%s", line);
    g_log_total++;
    if (g_log_n < MAX_LOG_LINES) g_log_n++;
}

static void log_replace_last(const char *line) {
    if (g_log_total == 0) { log_append(line); return; }
    int slot = (g_log_total - 1) % MAX_LOG_LINES;
    snprintf(g_log[slot], sizeof(g_log[slot]), "%s", line);
}

static void install_redraw_log(void) {
    int rows_avail = g_step_rows_avail;
    int start = (g_log_n > rows_avail) ? (g_log_total - rows_avail) : (g_log_total - g_log_n);
    if (start < 0) start = 0;
    for (int r = 0; r < rows_avail; r++) {
        go_xy(g_step_content_row + r, g_step_pane_col);
        for (int i = 0; i < g_step_pane_w - 1; i++) putchar(' ');
        go_xy(g_step_content_row + r, g_step_pane_col);
        int idx = start + r;
        if (idx >= 0 && idx < g_log_total) {
            int slot = idx % MAX_LOG_LINES;
            fputs(g_log[slot], stdout);
        }
    }
    hide_cursor();
    fflush(stdout);
}

static void step_begin(const char *desc) {
    g_step_n++;
    char buf[96];
    snprintf(buf, sizeof(buf), C_YELLOW " [%d/%d] %s..." C_RESET,
             g_step_n, g_step_total, desc);
    log_append(buf);
    install_redraw_log();
}

static void step_ok(const char *desc) {
    char buf[96];
    snprintf(buf, sizeof(buf), C_GREEN " [%d/%d]" C_RESET " %s " C_GREEN "OK" C_RESET,
             g_step_n, g_step_total, desc);
    log_replace_last(buf);
    install_redraw_log();
}

static void step_fail(const char *desc, int rc) {
    char buf[96];
    snprintf(buf, sizeof(buf), C_RED " [%d/%d]" C_RESET " %s " C_RED "FAIL rc=%d" C_RESET,
             g_step_n, g_step_total, desc, rc);
    log_replace_last(buf);
    install_redraw_log();
}

static int ensure_dir(const char *p) {
    struct stat st;
    if (stat(p, &st) == 0) return 0;
    return mkdir(p, 0755);
}

static int ensure_parent_dir(const char *path) {
    char tmp[512];
    size_t len = strlen(path);
    if (len >= sizeof(tmp)) return -1;
    int last_slash = -1;
    for (int i = (int)len - 1; i >= 0; i--) {
        if (path[i] == '/') { last_slash = i; break; }
    }
    if (last_slash <= 0) return 0;
    for (int i = 0; i < last_slash; i++) tmp[i] = path[i];
    tmp[last_slash] = '\0';
    int depth = 0;
    int starts[32];
    starts[depth++] = 0;
    for (int i = 1; i < last_slash && depth < 32; i++) {
        if (tmp[i] == '/') starts[depth++] = i;
    }
    for (int d = 0; d < depth; d++) {
        int end = (d + 1 < depth) ? starts[d + 1] : last_slash;
        char part[512];
        for (int i = 0; i < end; i++) part[i] = tmp[i];
        part[end] = '\0';
        if (part[0] == '\0') continue;
        struct stat st;
        if (stat(part, &st) != 0) syscall2(SYS_MKDIR, part, 0755);
    }
    return 0;
}

static int copy_one_file(const char *src, const char *dst) {
    int sfd = open(src, O_RDONLY, 0);
    if (sfd < 0) return sfd;
    ensure_parent_dir(dst);
    int dfd = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0755);
    if (dfd < 0) { close(sfd); return dfd; }
    static char fbuf[8192];
    ssize_t n;
    int rc = 0;
    while ((n = read(sfd, fbuf, sizeof(fbuf))) > 0) {
        ssize_t w = write(dfd, fbuf, (size_t)n);
        if (w < 0) { rc = (int)w; break; }
    }
    close(sfd);
    close(dfd);
    return rc;
}

static int read_dir_entries(const char *path, dent_t *out, int max) {
    DIR *d = opendir(path);
    if (!d) return 0;
    int count = 0;
    struct dirent *de;
    while (count < max && (de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.' &&
            (de->d_name[1] == '\0' ||
             (de->d_name[1] == '.' && de->d_name[2] == '\0'))) continue;
        size_t l = strlen(de->d_name);
        if (l >= sizeof(out[count].d_name)) l = sizeof(out[count].d_name) - 1;
        memcpy(out[count].d_name, de->d_name, l);
        out[count].d_name[l] = 0;
        out[count].d_type = de->d_type;
        count++;
    }
    closedir(d);
    return count;
}

static int should_skip_entry(const char *name) {
    if (strcmp(name, "cervus-installer") == 0) return 1;
    if (strcmp(name, "cervus-installer.elf") == 0) return 1;
    if (strcmp(name, "install-on-disk") == 0) return 1;
    return 0;
}

static void copy_tree(const char *src_dir, const char *dst_dir) {
    dent_t *entries = malloc(MAX_ENTRIES * sizeof(dent_t));
    if (!entries) return;
    int n = read_dir_entries(src_dir, entries, MAX_ENTRIES);
    if (n == 0) { free(entries); return; }
    ensure_dir(dst_dir);
    for (int i = 0; i < n; i++) {
        if (should_skip_entry(entries[i].d_name)) continue;
        char sp[512], dp[512];
        path_join(src_dir, entries[i].d_name, sp, sizeof(sp));
        path_join(dst_dir, entries[i].d_name, dp, sizeof(dp));
        if (entries[i].d_type == 1) {
            ensure_dir(dp);
            copy_tree(sp, dp);
        } else {
            copy_one_file(sp, dp);
        }
    }
    free(entries);
}

static int write_limine_conf(const char *path) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return fd;
    const char *conf =
        "timeout: 5\n"
        "default_entry: 1\n"
        "interface_branding: Cervus\n"
        "wallpaper: boot():/boot/wallpaper.png\n"
        "\n"
        "/Cervus v0.0.2 Alpha\n"
        "    protocol: limine\n"
        "    path: boot():/kernel\n"
        "    module_path: boot():/shell.elf\n"
        "    module_cmdline: init\n";
    write(fd, conf, strlen(conf));
    close(fd);
    return 0;
}

static int do_install(const disk_entry_t *d, const layout_t *L) {
    uint64_t total_sectors = d->sectors;
    if (total_sectors > 0xFFFFFFFEULL) total_sectors = 0xFFFFFFFEULL;

    uint32_t esp_start  = 2048;
    uint32_t esp_size   = L->esp_mb * 2048;
    uint32_t root_start = esp_start + esp_size;
    uint32_t swap_size  = L->have_swap ? (L->swap_mb * 2048) : 0;
    if ((uint64_t)root_start + swap_size + 32768 > total_sectors) {
        hide_cursor();
        clear_screen();
        go_xy(g_rows / 2, g_pane_left_col);
        fputs(C_RED "Root would be too small - adjust layout" C_RESET, stdout);
        fflush(stdout);
        read_key();
        return 1;
    }
    uint32_t avail      = (uint32_t)total_sectors - root_start - swap_size;
    uint32_t root_size  = avail;
    uint32_t swap_start = root_start + root_size;

    install_screen_init(8);

    step_begin("Writing partition table");
    cervus_mbr_part_t specs[4];
    memset(specs, 0, sizeof(specs));
    int n_parts = 0;
    specs[n_parts].boot_flag    = 1;
    specs[n_parts].type         = MBR_TYPE_FAT32_LBA;
    specs[n_parts].lba_start    = esp_start;
    specs[n_parts].sector_count = esp_size;
    n_parts++;
    specs[n_parts].boot_flag    = 0;
    specs[n_parts].type         = MBR_TYPE_LINUX;
    specs[n_parts].lba_start    = root_start;
    specs[n_parts].sector_count = root_size;
    n_parts++;
    if (L->have_swap) {
        specs[n_parts].boot_flag    = 0;
        specs[n_parts].type         = MBR_TYPE_LINUX_SWAP;
        specs[n_parts].lba_start    = swap_start;
        specs[n_parts].sector_count = swap_size;
        n_parts++;
    }
    int rc = cervus_disk_partition(d->name, specs, n_parts);
    if (rc < 0) { step_fail("partition table", rc); read_key(); return 1; }
    step_ok("partition table");

    char part1[40], part2[40];
    make_part_name(d->name, 1, part1, sizeof(part1));
    make_part_name(d->name, 2, part2, sizeof(part2));

    step_begin("Formatting ESP (FAT32)");
    rc = cervus_disk_mkfs_fat32(part1, "CERVUS-ESP");
    if (rc < 0) { step_fail("ESP format", rc); read_key(); return 1; }
    step_ok("ESP formatted");

    step_begin("Formatting root (ext2)");
    rc = cervus_disk_format(part2, "cervus-root");
    if (rc < 0) { step_fail("root format", rc); read_key(); return 1; }
    step_ok("root formatted");

    step_begin("Mounting partitions");
    ensure_dir("/mnt");
    ensure_dir("/mnt/esp");
    ensure_dir("/mnt/root");
    rc = cervus_disk_mount(part1, "/mnt/esp");
    if (rc < 0) { step_fail("mount ESP", rc); read_key(); return 1; }
    rc = cervus_disk_mount(part2, "/mnt/root");
    if (rc < 0) { step_fail("mount root", rc); cervus_disk_umount("/mnt/esp"); read_key(); return 1; }
    step_ok("mounted");

    step_begin("Copying boot files");
    ensure_dir("/mnt/esp/boot");
    ensure_dir("/mnt/esp/boot/limine");
    ensure_dir("/mnt/esp/EFI");
    ensure_dir("/mnt/esp/EFI/BOOT");
    struct { const char *src; const char *dst; int req; } boot_files[] = {
        { "/boot/kernel",              "/mnt/esp/boot/kernel",                        1 },
        { "/boot/kernel",              "/mnt/esp/kernel",                             0 },
        { "/boot/shell.elf",           "/mnt/esp/boot/shell.elf",                     1 },
        { "/boot/shell.elf",           "/mnt/esp/shell.elf",                          0 },
        { "/boot/limine-bios.sys",     "/mnt/esp/boot/limine/limine-bios.sys",        0 },
        { "/boot/limine-bios-hdd.bin", "/mnt/esp/boot/limine/limine-bios-hdd.bin",    0 },
        { "/boot/BOOTX64.EFI",         "/mnt/esp/EFI/BOOT/BOOTX64.EFI",               0 },
        { "/boot/BOOTIA32.EFI",        "/mnt/esp/EFI/BOOT/BOOTIA32.EFI",              0 },
        { "/boot/wallpaper.png",       "/mnt/esp/boot/wallpaper.png",                 0 },
        { "/boot/wallpaper.png",       "/mnt/esp/wallpaper.png",                      0 },
        { NULL, NULL, 0 }
    };
    int required_missing = 0;
    for (int i = 0; boot_files[i].src; i++) {
        struct stat st;
        if (stat(boot_files[i].src, &st) != 0) {
            if (boot_files[i].req) required_missing++;
            continue;
        }
        copy_one_file(boot_files[i].src, boot_files[i].dst);
    }
    if (required_missing > 0) {
        step_fail("boot files missing", -1);
        cervus_disk_umount("/mnt/esp");
        cervus_disk_umount("/mnt/root");
        read_key();
        return 1;
    }
    step_ok("boot files copied");

    step_begin("Populating root filesystem");
    const char *rdirs[] = {
        "/mnt/root/bin", "/mnt/root/apps", "/mnt/root/etc",
        "/mnt/root/home", "/mnt/root/tmp", "/mnt/root/var",
        "/mnt/root/usr", NULL
    };
    for (int i = 0; rdirs[i]; i++) ensure_dir(rdirs[i]);
    copy_tree("/bin",  "/mnt/root/bin");
    copy_tree("/apps", "/mnt/root/apps");
    struct stat ust;
    if (stat("/usr", &ust) == 0) copy_tree("/usr", "/mnt/root/usr");
    struct stat est;
    if (stat("/etc", &est) == 0) {
        static dent_t etc_entries[MAX_ENTRIES];
        int nn = read_dir_entries("/etc", etc_entries, MAX_ENTRIES);
        for (int i = 0; i < nn; i++) {
            const char *nm = etc_entries[i].d_name;
            size_t nl = strlen(nm);
            int is_txt = (nl >= 4 && strcmp(nm + nl - 4, ".txt") == 0);
            char sp[256], dp[256];
            path_join("/etc", nm, sp, sizeof(sp));
            if (etc_entries[i].d_type == 0) {
                if (is_txt) path_join("/mnt/root/home", nm, dp, sizeof(dp));
                else        path_join("/mnt/root/etc",  nm, dp, sizeof(dp));
                copy_one_file(sp, dp);
            } else if (etc_entries[i].d_type == 1) {
                path_join("/mnt/root/etc", nm, dp, sizeof(dp));
                copy_tree(sp, dp);
            }
        }
    }
    struct stat hst;
    if (stat("/home", &hst) == 0) copy_tree("/home", "/mnt/root/home");
    step_ok("root populated");

    step_begin("Writing limine.conf");
    write_limine_conf("/mnt/esp/boot/limine/limine.conf");
    write_limine_conf("/mnt/esp/EFI/BOOT/limine.conf");
    write_limine_conf("/mnt/esp/limine.conf");
    step_ok("limine.conf written");

    step_begin("Installing BIOS stage1 (MBR)");
    {
        static uint8_t sys_buf[300 * 1024];
        int fd = open("/mnt/esp/boot/limine/limine-bios-hdd.bin", O_RDONLY, 0);
        if (fd < 0) {
            step_fail("stage1 not found", -1);
        } else {
            struct stat st;
            int sr = stat("/mnt/esp/boot/limine/limine-bios-hdd.bin", &st);
            uint32_t sys_size = (sr == 0) ? (uint32_t)st.st_size : 0;
            if (sys_size == 0 || sys_size > sizeof(sys_buf)) {
                close(fd);
                step_fail("bad stage1 size", -1);
            } else {
                uint32_t got = 0;
                int ok = 1;
                while (got < sys_size) {
                    ssize_t n = read(fd, sys_buf + got, sys_size - got);
                    if (n <= 0) { ok = 0; break; }
                    got += (uint32_t)n;
                }
                close(fd);
                if (!ok) step_fail("short read on stage1", -1);
                else {
                    long ir = cervus_disk_bios_install(d->name, sys_buf, sys_size);
                    if (ir < 0) step_fail("bios_install syscall", (int)ir);
                    else        step_ok("BIOS stage1 installed");
                }
            }
        }
    }

    cervus_disk_umount("/mnt/esp");
    cervus_disk_umount("/mnt/root");

    go_xy(g_step_content_row + g_step_rows_avail + 1, g_step_pane_col);
    fputs(C_GREEN C_BOLD "Installation complete!" C_RESET, stdout);
    hint_line("Enter reboots, Esc returns to Live mode");
    fflush(stdout);

    int k = read_key();
    if (k == KEY_ESC) return 0;
    syscall0(SYS_REBOOT);
    return 0;
}

static int choose_disk_auto(const disk_entry_t *disks, int n) {
    int best = -1;
    uint64_t best_sz = 0;
    for (int i = 0; i < n; i++) {
        if (disks[i].kind == DISK_KIND_INTERNAL && disks[i].size_bytes > best_sz) {
            best = i;
            best_sz = disks[i].size_bytes;
        }
    }
    if (best >= 0) return best;
    for (int i = 0; i < n; i++) {
        if (disks[i].kind == DISK_KIND_USB && disks[i].size_bytes > best_sz) {
            best = i;
            best_sz = disks[i].size_bytes;
        }
    }
    return best;
}

static int detect_existing_install(const disk_entry_t *disks, int n) {
    for (int i = 0; i < n; i++) {
        if (disks[i].kind == DISK_KIND_OTHER) continue;
        char p2[40];
        make_part_name(disks[i].name, 2, p2, sizeof(p2));
        char devpath[64];
        snprintf(devpath, sizeof(devpath), "/dev/%s", p2);
        struct stat ds;
        if (stat(devpath, &ds) != 0) continue;
        if (cervus_disk_mount(p2, "/mnt") == 0) {
            struct stat sh;
            int has_shell = (stat("/mnt/bin/shell", &sh) == 0);
            cervus_disk_umount("/mnt");
            if (has_shell) return 1;
        }
    }
    return 0;
}

static int do_main_install_flow(disk_entry_t *disks, int n_disks) {
    layout_t L;
    memset(&L, 0, sizeof(L));
    L.esp_mb    = 64;
    L.have_swap = 1;
    L.swap_mb   = 16;

    for (;;) {
        const char *mode_items[] = {
            "Automatic    (best internal disk + defaults)",
            "Manual       (choose disk and tune layout)",
            "Install to USB flash drive",
            "Back",
        };
        int mode = menu_in_box("Cervus OS Installer", mode_items, 4, 0, info_mode);
        if (mode < 0 || mode == 3) return EXIT_CANCEL;

        int picked = -1;
        if (mode == 0) {
            picked = choose_disk_auto(disks, n_disks);
            if (picked < 0) {
                hide_cursor();
                clear_screen();
                go_xy(g_rows / 2, g_pane_left_col);
                fputs(C_RED "No suitable disk for automatic install" C_RESET, stdout);
                fflush(stdout);
                read_key();
                continue;
            }
        } else if (mode == 1) {
            picked = disk_select_in_box("Select target disk",
                                        disks, n_disks, 0, info_disk);
            if (picked < 0) continue;
        } else if (mode == 2) {
            disk_entry_t usb[MAX_DISKS];
            int n_usb = 0;
            for (int i = 0; i < n_disks; i++)
                if (disks[i].kind == DISK_KIND_USB) usb[n_usb++] = disks[i];
            if (n_usb == 0) {
                hide_cursor();
                clear_screen();
                go_xy(g_rows / 2, g_pane_left_col);
                fputs(C_YELLOW "No USB flash drives detected" C_RESET, stdout);
                fflush(stdout);
                read_key();
                continue;
            }
            int pu = disk_select_in_box("Select USB flash drive",
                                        usb, n_usb, 0, info_disk);
            if (pu < 0) continue;
            for (int i = 0; i < n_disks; i++)
                if (strcmp(disks[i].name, usb[pu].name) == 0) { picked = i; break; }
            if (picked < 0) continue;
        }

        if (mode == 1) {
            if (manual_layout(&disks[picked], &L) < 0) continue;
        } else {
            uint32_t total_mb = (uint32_t)(disks[picked].size_bytes / (1024 * 1024));
            L.esp_mb    = (total_mb < 256) ? 32 : 64;
            L.have_swap = (total_mb >= 128) ? 1 : 0;
            L.swap_mb   = 16;
            L.total_mb  = total_mb;
        }

        int conf = confirm_screen(&disks[picked], &L);
        if (conf != 1) continue;

        do_install(&disks[picked], &L);
        return EXIT_CANCEL;
    }
}

static int boot_prompt_flow(void) {
    disk_entry_t disks[MAX_DISKS];
    int n_disks = list_disks(disks, MAX_DISKS);
    if (n_disks == 0) return EXIT_LIVE;

    int has_existing = detect_existing_install(disks, n_disks);

    for (;;) {
        const char *items_existing[] = {
            "Continue in Live mode",
            "Boot the installed system (mount /mnt)",
            "Reinstall Cervus (erase the existing install)",
            "Exit (back to shell)",
        };
        const char *items_fresh[] = {
            "Continue in Live mode",
            "Install Cervus to a disk",
            "Exit (back to shell)",
        };

        int n_items = has_existing ? 4 : 3;
        const char **items = has_existing ? items_existing : items_fresh;
        int sel = menu_in_box("Welcome to Cervus", items, n_items, 0, info_boot);
        if (sel < 0) return EXIT_LIVE;

        if (has_existing) {
            if (sel == 0) return EXIT_LIVE;
            if (sel == 1) return EXIT_MOUNT_EXISTING;
            if (sel == 2) {
                int rc = do_main_install_flow(disks, n_disks);
                if (rc == EXIT_CANCEL) continue;
                return rc;
            }
            if (sel == 3) return EXIT_LIVE;
        } else {
            if (sel == 0) return EXIT_LIVE;
            if (sel == 1) {
                int rc = do_main_install_flow(disks, n_disks);
                if (rc == EXIT_CANCEL) continue;
                return rc;
            }
            if (sel == 2) return EXIT_LIVE;
        }
    }
}

static int normal_install_flow(void) {
    disk_entry_t disks[MAX_DISKS];
    int n_disks = list_disks(disks, MAX_DISKS);
    if (n_disks == 0) {
        hide_cursor();
        clear_screen();
        go_xy(g_rows / 2, g_pane_left_col);
        fputs(C_RED "  No disks detected!" C_RESET, stdout);
        fflush(stdout);
        read_key();
        return EXIT_CANCEL;
    }
    return do_main_install_flow(disks, n_disks);
}

int main(int argc, char **argv) {
    const char *mode = getenv_argv(argc, argv, "MODE", "");
    if (strcmp(mode, "live") != 0) {
        fputs(C_RED "cervus-installer: only available in Live mode.\n" C_RESET, stderr);
        return 1;
    }

    const char *boot = getenv_argv(argc, argv, "BOOT_PROMPT", "");
    int boot_mode = (boot && boot[0] == '1');

    term_size_query();
    term_enter_raw();
    int rc;
    if (boot_mode) rc = boot_prompt_flow();
    else           rc = normal_install_flow();
    hide_cursor();
    clear_screen();
    term_restore();
    return rc;
}
