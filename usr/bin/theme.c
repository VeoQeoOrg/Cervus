#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/syscall.h>
#include <dirent.h>
#include <sys/stat.h>
#include <curses.h>
#include <pwutil.h>

#define THEME_CONF "/etc/console.conf"
#define THEME_DIR  "/etc/themes"
#define MAX_CUSTOM 32

typedef struct {
    uint32_t palette[16];
    uint32_t fg;
    uint32_t bg;
} theme_t;

typedef struct {
    const char *name;
    const char *about;
    theme_t     t;
} named_theme_t;

static const named_theme_t THEMES[] = {
{ "classic", "the original VGA palette", {{
    0x000000, 0xAA0000, 0x00AA00, 0xAA5500, 0x0000AA, 0xAA00AA, 0x00AAAA, 0xAAAAAA,
    0x555555, 0xFF5555, 0x55FF55, 0xFFFF55, 0x5555FF, 0xFF55FF, 0x55FFFF, 0xFFFFFF },
    0xFFFFFF, 0x000000 }},

{ "cervus", "soft slate, easy on the eyes", {{
    0x2E3440, 0xBF616A, 0xA3BE8C, 0xEBCB8B, 0x81A1C1, 0xB48EAD, 0x88C0D0, 0xD8DEE9,
    0x4C566A, 0xD08770, 0xB9D4A0, 0xF0D399, 0x9BB8DA, 0xC7A4C0, 0xA3D4E0, 0xECEFF4 },
    0xD8DEE9, 0x21252E }},

{ "gruv", "warm and low contrast", {{
    0x282828, 0xCC241D, 0x98971A, 0xD79921, 0x458588, 0xB16286, 0x689D6A, 0xA89984,
    0x928374, 0xFB4934, 0xB8BB26, 0xFABD2F, 0x83A598, 0xD3869B, 0x8EC07C, 0xEBDBB2 },
    0xEBDBB2, 0x282828 }},

{ "solar", "muted blue-grey, low glare", {{
    0x073642, 0xDC322F, 0x859900, 0xB58900, 0x268BD2, 0xD33682, 0x2AA198, 0xEEE8D5,
    0x586E75, 0xCB4B16, 0x93A1A1, 0x657B83, 0x839496, 0x6C71C4, 0x93A1A1, 0xFDF6E3 },
    0x93A1A1, 0x002B36 }},

{ "paper", "dark text on a light page", {{
    0xEEEEEC, 0xA40000, 0x4E9A06, 0xC4A000, 0x3465A4, 0x75507B, 0x06989A, 0x2E3436,
    0xD3D7CF, 0xCC0000, 0x73D216, 0xEDD400, 0x729FCF, 0xAD7FA8, 0x34E2E2, 0x000000 },
    0x2E3436, 0xF2F1EC }},

{ "matrix", "green on black, nothing else", {{
    0x001B00, 0x2E7D32, 0x00E676, 0x00C853, 0x1B5E20, 0x33691E, 0x00BFA5, 0x69F0AE,
    0x0A3D0A, 0x4CAF50, 0x76FF03, 0x64DD17, 0x2E7D32, 0x00E676, 0x1DE9B6, 0xB9F6CA },
    0x00E676, 0x000A00 }},

{ "dracula", "the usual purple night", {{
    0x282A36, 0xFF5555, 0x50FA7B, 0xF1FA8C, 0xBD93F9, 0xFF79C6, 0x8BE9FD, 0xF8F8F2,
    0x6272A4, 0xFF6E6E, 0x69FF94, 0xFFFFA5, 0xD6ACFF, 0xFF92DF, 0xA4FFFF, 0xFFFFFF },
    0xF8F8F2, 0x282A36 }},

{ "catppuccin", "mocha, warm and muted", {{
    0x45475A, 0xF38BA8, 0xA6E3A1, 0xF9E2AF, 0x89B4FA, 0xF5C2E7, 0x94E2D5, 0xBAC2DE,
    0x585B70, 0xF37799, 0xB2E8AD, 0xFAE7BE, 0x9CC0FB, 0xF7CEEB, 0xA5E7DC, 0xA6ADC8 },
    0xCDD6F4, 0x1E1E2E }},

{ "latte", "catppuccin in daylight", {{
    0x5C5F77, 0xD20F39, 0x40A02B, 0xDF8E1D, 0x1E66F5, 0xEA76CB, 0x179299, 0xACB0BE,
    0x6C6F85, 0xD52A44, 0x49AF3D, 0xE0913C, 0x456EFF, 0xEC83D0, 0x2D9FA8, 0xBCC0CC },
    0x4C4F69, 0xEFF1F5 }},

{ "nord", "cold blue, low contrast", {{
    0x3B4252, 0xBF616A, 0xA3BE8C, 0xEBCB8B, 0x81A1C1, 0xB48EAD, 0x88C0D0, 0xE5E9F0,
    0x4C566A, 0xBF616A, 0xA3BE8C, 0xEBCB8B, 0x81A1C1, 0xB48EAD, 0x8FBCBB, 0xECEFF4 },
    0xD8DEE9, 0x2E3440 }},

{ "rose", "dusty pink and mauve", {{
    0x26233A, 0xEB6F92, 0x9CCFD8, 0xF6C177, 0x31748F, 0xC4A7E7, 0xEBBCBA, 0xE0DEF4,
    0x6E6A86, 0xEB6F92, 0x9CCFD8, 0xF6C177, 0x31748F, 0xC4A7E7, 0xEBBCBA, 0xE0DEF4 },
    0xE0DEF4, 0x191724 }},

{ "sand", "pastel, paper and clay", {{
    0x5B5147, 0xC96F5B, 0x7D9C6B, 0xD9A05B, 0x6B8CA3, 0xA88BA3, 0x77A6A0, 0xD8CFC2,
    0x7A6E62, 0xE08A72, 0x95B884, 0xEFBC78, 0x86A6BE, 0xC3A5BE, 0x93C0BA, 0xF2EAE0 },
    0xE8DFD2, 0x2B2622 }},

{ "mint", "pastel green and teal", {{
    0x2F3B36, 0xE8837E, 0x8FD9A8, 0xEBD292, 0x7FB2CE, 0xC4A0D6, 0x8FD6D2, 0xDCE8E2,
    0x46564F, 0xF29A95, 0xA8E8BE, 0xF5E2AC, 0x9CC8E0, 0xD6B8E5, 0xAAE5E2, 0xF0F7F3 },
    0xDCE8E2, 0x1F2A26 }},

{ "amber", "old amber terminal", {{
    0x2A1B00, 0xB33A00, 0xC98A00, 0xFFB000, 0x8A5A00, 0xB36A00, 0xD9A400, 0xE8C070,
    0x4A3200, 0xFF6A00, 0xFFC947, 0xFFD966, 0xC98A00, 0xFF9E3D, 0xFFDD8A, 0xFFF0C4 },
    0xFFB000, 0x1A1000 }},

{ "mono", "grey scale only", {{
    0x101010, 0x8A8A8A, 0xB0B0B0, 0xC8C8C8, 0x707070, 0x989898, 0xC0C0C0, 0xD8D8D8,
    0x505050, 0xA8A8A8, 0xCFCFCF, 0xE0E0E0, 0x909090, 0xB8B8B8, 0xDCDCDC, 0xF4F4F4 },
    0xCFCFCF, 0x141414 }},
};

#define NBUILTIN ((int)(sizeof THEMES / sizeof THEMES[0]))

static named_theme_t g_custom[MAX_CUSTOM];
static char          g_custom_name[MAX_CUSTOM][32];
static char          g_custom_about[MAX_CUSTOM][64];
static int           g_ncustom;

static int parse_hex(const char *s, uint32_t *out);

static void load_custom(void) {
    g_ncustom = 0;
    DIR *d = opendir(THEME_DIR);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL && g_ncustom < MAX_CUSTOM) {
        size_t n = strlen(e->d_name);
        if (n < 7 || strcmp(e->d_name + n - 6, ".theme")) continue;

        char path[256];
        snprintf(path, sizeof path, "%s/%s", THEME_DIR, e->d_name);
        FILE *f = fopen(path, "r");
        if (!f) continue;

        named_theme_t nt;
        memset(&nt, 0, sizeof nt);
        char *nm = g_custom_name[g_ncustom];
        char *ab = g_custom_about[g_ncustom];
        snprintf(nm, 32, "%.*s", (int)(n - 6), e->d_name);
        snprintf(ab, 64, "%s", "custom");

        char line[512];
        while (fgets(line, sizeof line, f)) {
            char *nl = strchr(line, '\n'); if (nl) *nl = 0;
            char *eq = strchr(line, '='); if (!eq) continue;
            *eq = 0;
            const char *v = eq + 1;
            if (!strcmp(line, "about")) snprintf(ab, 64, "%s", v);
            else if (!strcmp(line, "fg")) parse_hex(v, &nt.t.fg);
            else if (!strcmp(line, "bg")) parse_hex(v, &nt.t.bg);
            else if (!strcmp(line, "palette")) {
                const char *p = v;
                for (int i = 0; i < 16 && p && *p; i++) {
                    char tok[16]; int k = 0;
                    while (*p && *p != ',' && k < 15) tok[k++] = *p++;
                    tok[k] = 0;
                    parse_hex(tok, &nt.t.palette[i]);
                    if (*p == ',') p++;
                }
            }
        }
        fclose(f);
        nt.name  = nm;
        nt.about = ab;
        g_custom[g_ncustom++] = nt;
    }
    closedir(d);
}

static int total_themes(void) { return NBUILTIN + g_ncustom; }

static const named_theme_t *theme_at(int i) {
    if (i < NBUILTIN) return &THEMES[i];
    return &g_custom[i - NBUILTIN];
}

static const char USAGE[] =
"theme - the colours the console draws with\n"
"\n"
"Usage\n"
"  theme                    list the schemes, marking the one in use\n"
"  theme --list             one name and description per line\n"
"  theme <name>             switch to it, and use it again after a reboot\n"
"  theme <name> --once      switch without remembering\n"
"  theme edit [name]        open the editor, starting from <name>\n"
"  theme bg #RRGGBB         change only the background\n"
"  theme fg #RRGGBB         change only the text colour\n"
"  theme -r                 back to the original palette\n"
"\n"
"The editor\n"
"  up down                  choose one of the eighteen colours\n"
"  left right               choose the red, green or blue channel\n"
"  Enter, or any digit      type that channel, 0 to 255\n"
"  -  +                     change it by one\n"
"  _  =                     change it by sixteen\n"
"  h                        type the whole colour, like #1E1E2E\n"
"  n                        name the scheme\n"
"  s                        save it\n"
"  q                        leave\n"
"\n"
"  The console follows every change as it is made, so the editor is\n"
"  drawn in the scheme being edited. Nothing is written until s.\n"
"\n"
"What the colours are for\n"
"  Sixteen palette entries plus two defaults. Programs ask for a\n"
"  palette entry by meaning, not by value: red for errors, green for\n"
"  the prompt and executables, blue for directories, cyan for status\n"
"  bars. The two defaults are the text colour where nothing else is\n"
"  set, and the background. The editor lists the use beside each one.\n"
"\n"
"Files\n"
"  /etc/console.conf        the chosen scheme, restored at boot\n"
"  /etc/themes/*.theme      schemes made here, listed with the rest\n";

static theme_t g_cur;
static char    g_cur_name[32] = "classic";

static int parse_hex(const char *s, uint32_t *out) {
    if (*s == '#') s++;
    uint32_t v = 0;
    int n = 0;
    for (; *s; s++, n++) {
        char c = *s;
        int d;
        if      (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else return -1;
        v = (v << 4) | (uint32_t)d;
    }
    if (n != 6) return -1;
    *out = v;
    return 0;
}

static const named_theme_t *find_theme(const char *name) {
    for (int i = 0; i < total_themes(); i++)
        if (!strcmp(theme_at(i)->name, name)) return theme_at(i);
    return NULL;
}

int theme_apply_live(const theme_t *t) {
    return (int)syscall1(SYS_CONSOLE_THEME, t);
}

int theme_lookup(const char *name, theme_t *out) {
    load_custom();
    const named_theme_t *nt = find_theme(name);
    if (!nt) return -1;
    *out = nt->t;
    return 0;
}

static int apply(const theme_t *t) {
    if (syscall1(SYS_CONSOLE_THEME, t) != 0) {
        fprintf(stderr, "theme: the console rejected it\n");
        return -1;
    }
    return 0;
}

static void load_conf(void) {
    g_cur = THEMES[0].t;
    FILE *f = fopen("/mnt" THEME_CONF, "r");
    if (!f) f = fopen(THEME_CONF, "r");
    if (!f) return;
    char line[128];
    while (fgets(line, sizeof line, f)) {
        char *nl = strchr(line, '\n'); if (nl) *nl = 0;
        char *eq = strchr(line, '='); if (!eq) continue;
        *eq = 0;
        const char *v = eq + 1;
        if (!strcmp(line, "theme")) {
            const named_theme_t *nt = find_theme(v);
            if (nt) { g_cur = nt->t; snprintf(g_cur_name, sizeof g_cur_name, "%s", v); }
        } else if (!strcmp(line, "bg")) {
            uint32_t c; if (parse_hex(v, &c) == 0) g_cur.bg = c;
        } else if (!strcmp(line, "fg")) {
            uint32_t c; if (parse_hex(v, &c) == 0) g_cur.fg = c;
        }
    }
    fclose(f);
}

static int save_conf(void) {
    FILE *f = fopen(THEME_CONF, "w");
    if (!f) { priv_denied(THEME_CONF); return -1; }
    fprintf(f, "theme=%s\n", g_cur_name);
    fprintf(f, "fg=%06X\n", g_cur.fg);
    fprintf(f, "bg=%06X\n", g_cur.bg);
    fprintf(f, "palette=");
    for (int i = 0; i < 16; i++) fprintf(f, "%06X%s", g_cur.palette[i], i < 15 ? "," : "\n");
    fclose(f);
    return 0;
}

static void show(void) {
    printf("current: \x1b[1m%s\x1b[0m   text #%06X on #%06X\n\n",
           g_cur_name, g_cur.fg, g_cur.bg);
    printf("available:\n");
    for (int i = 0; i < total_themes(); i++) {
        const theme_t *t = &theme_at(i)->t;
        printf("  %-11s %-31s ", theme_at(i)->name, theme_at(i)->about);
        for (int c = 1; c < 8; c++) {
            uint32_t v = t->palette[c];
            printf("\x1b[48;2;%u;%u;%um  \x1b[0m",
                   (v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
        }
        for (int c = 9; c < 15; c++) {
            uint32_t v = t->palette[c];
            printf("\x1b[48;2;%u;%u;%um  \x1b[0m",
                   (v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
        }
        printf(" \x1b[48;2;%u;%u;%um\x1b[38;2;%u;%u;%um Aa \x1b[0m",
               (t->bg >> 16) & 0xFF, (t->bg >> 8) & 0xFF, t->bg & 0xFF,
               (t->fg >> 16) & 0xFF, (t->fg >> 8) & 0xFF, t->fg & 0xFF);
        if (i >= NBUILTIN) printf(" \x1b[90mcustom\x1b[0m");
        printf("\n");
    }
    printf("\nrun 'theme <name>' to switch, 'theme edit <name>' to make one\n");
}

int theme_editor(const char *name);

int main(int argc, char **argv) {
    priv_argv(argc, argv);
    load_custom();
    load_conf();

    if (argc >= 2 && !strcmp(argv[1], "edit"))
        return theme_editor(argc >= 3 ? argv[2] : NULL);

    if (argc < 2) { show(); return 0; }
    if (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) { fputs(USAGE, stdout); return 0; }

    if (!strcmp(argv[1], "--restore")) {
        if (apply(&g_cur) != 0) return 1;
        if (getuid() == 0) save_conf();
        return 0;
    }
    if (!strcmp(argv[1], "--list")) {
        for (int i = 0; i < total_themes(); i++)
            printf("%s\t%s\n", theme_at(i)->name, theme_at(i)->about);
        return 0;
    }
    if (!strcmp(argv[1], "-r")) {
        g_cur = THEMES[0].t;
        snprintf(g_cur_name, sizeof g_cur_name, "classic");
        if (apply(&g_cur) != 0) return 1;
        unlink(THEME_CONF);
        return 0;
    }

    if (!strcmp(argv[1], "bg") || !strcmp(argv[1], "fg")) {
        if (argc < 3) { fputs(USAGE, stderr); return 1; }
        uint32_t c;
        if (parse_hex(argv[2], &c) != 0) {
            fprintf(stderr, "theme: %s is not a #RRGGBB colour\n", argv[2]);
            return 1;
        }
        if (argv[1][0] == 'b') g_cur.bg = c; else g_cur.fg = c;
        if (apply(&g_cur) != 0) return 1;
        save_conf();
        return 0;
    }

    const named_theme_t *nt = find_theme(argv[1]);
    if (!nt) {
        fprintf(stderr, "theme: no theme called %s\n", argv[1]);
        show();
        return 1;
    }
    g_cur = nt->t;
    snprintf(g_cur_name, sizeof g_cur_name, "%s", nt->name);
    if (apply(&g_cur) != 0) return 1;
    if (argc < 3 || strcmp(argv[2], "--once") != 0) save_conf();
    return 0;
}

static const char *SLOT_NAME[18] = {
    "black",   "red",     "green",   "yellow",
    "blue",    "magenta", "cyan",    "white",
    "br black","br red",  "br green","br yellow",
    "br blue", "br mag",  "br cyan", "br white",
    "text",    "background",
};

static const char *SLOT_USE[18] = {
    "the darkest entry; behind reversed text",
    "errors and warnings",
    "the shell prompt, and executables in ls",
    "device files, and highlights",
    "directories in ls and in the file manager",
    "symbolic links and special files",
    "status bars and headings",
    "ordinary text inside coloured output",
    "the bold form of black; dimmed text",
    "the bold form of red",
    "the bold form of green",
    "the bold form of yellow",
    "the bold form of blue",
    "the bold form of magenta",
    "the bold form of cyan",
    "the bold form of white; the brightest entry",
    "text where nothing has set a colour",
    "the console background, everywhere",
};

static uint32_t *slot_ptr(theme_t *t, int i) {
    if (i < 16) return &t->palette[i];
    if (i == 16) return &t->fg;
    return &t->bg;
}

static int mkdir_p_theme(void) {
    struct stat st;
    if (stat(THEME_DIR, &st) == 0) return 0;
    return mkdir(THEME_DIR, 0755);
}

static int save_theme(const char *name, const theme_t *t, const char *about) {
    if (mkdir_p_theme() != 0) return -1;
    char path[256];
    snprintf(path, sizeof path, "%s/%s.theme", THEME_DIR, name);
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fprintf(f, "about=%s\n", about && *about ? about : "custom");
    fprintf(f, "palette=");
    for (int i = 0; i < 16; i++)
        fprintf(f, "%06X%s", t->palette[i], i == 15 ? "\n" : ",");
    fprintf(f, "fg=%06X\n", t->fg);
    fprintf(f, "bg=%06X\n", t->bg);
    fclose(f);
    return 0;
}

static void draw_slot_swatch(int y, int x, int slot, int width) {
    attr_t a;
    if (slot < 16) {
        a = COLOR_PAIR(1 + (slot & 7));
        if (slot >= 8) a |= A_BOLD;
    } else if (slot == 16) {
        a = A_NORMAL;
    } else {
        a = A_REVERSE;
    }
    attron(a);
    move(y, x);
    for (int i = 0; i < width; i++) addch('#');
    attroff(a);
}

static int prompt_seeded(const char *label, char *buf, int cap, const char *seed) {
    int y = 1;
    int n = 0;
    buf[0] = 0;
    if (seed && seed[0]) { buf[0] = seed[0]; buf[1] = 0; n = 1; }

    for (;;) {
        move(y, 0);
        clrtoeol();
        attron(A_REVERSE);
        mvprintw(y, 0, " %s ", label);
        attroff(A_REVERSE);
        printw(" %s_", buf);
        refresh();

        int ch = getch();
        if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) break;
        if (ch == 27) {
            move(y, 0);
            clrtoeol();
            refresh();
            return -1;
        }
        if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
            if (n > 0) buf[--n] = 0;
            continue;
        }
        if (ch == ERR || ch == KEY_RESIZE) continue;
        if (ch >= 32 && ch < 127 && n < cap - 1) {
            buf[n++] = (char)ch;
            buf[n] = 0;
        }
    }

    move(y, 0);
    clrtoeol();
    refresh();
    return n;
}

static int prompt_line(const char *label, char *buf, int cap) {
    return prompt_seeded(label, buf, cap, NULL);
}

int theme_editor(const char *name) {
    theme_t t;
    char tname[32] = "mytheme";
    char about[64] = "custom";

    if (name && *name) {
        snprintf(tname, sizeof tname, "%s", name);
        if (theme_lookup(name, &t) != 0) {
            fprintf(stderr, "theme: no theme called %s to start from\n", name);
            return 1;
        }
    } else {
        theme_lookup("classic", &t);
    }

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, 1);
    curs_set(0);
    start_color();
    for (short i = 0; i < 8; i++) init_pair((short)(1 + i), i, COLOR_BLACK);

    int sel = 0, chan = 0, dirty = 0;
    theme_t live = t;
    theme_apply_live(&live);

    for (;;) {
        int rows = getmaxy(stdscr), cols = getmaxx(stdscr);
        int help_top = rows - 8;
        erase();

        attron(A_REVERSE);
        move(0, 0);
        for (int x = 0; x < cols; x++) addch(' ');
        mvprintw(0, 2, "theme editor");
        mvprintw(0, 17, "%s", tname);
        if (dirty) mvprintw(0, 17 + (int)strlen(tname) + 2, "(unsaved)");
        attroff(A_REVERSE);

        mvprintw(2, 2, "colour");
        mvprintw(2, 16, "swatch");
        mvprintw(2, 25, "value");
        mvprintw(2, 35, "red  green blue");
        mvprintw(2, 54, "where it is used");
        move(3, 2);
        for (int x = 2; x < cols - 2; x++) addch('-');

        for (int i = 0; i < 18; i++) {
            int y = 4 + i;
            if (y >= help_top - 1) break;
            uint32_t c = *slot_ptr(&t, i);

            if (i == sel) attron(A_REVERSE);
            mvprintw(y, 2, " %-11s ", SLOT_NAME[i]);
            if (i == sel) attroff(A_REVERSE);

            draw_slot_swatch(y, 16, i, 6);
            mvprintw(y, 25, "#%06X", c);

            for (int k = 0; k < 3; k++) {
                int v = (int)((c >> ((2 - k) * 8)) & 0xFF);
                int x = 35 + k * 6;
                if (i == sel && k == chan) attron(A_REVERSE);
                mvprintw(y, x, "%3d", v);
                if (i == sel && k == chan) attroff(A_REVERSE);
            }

            if (cols > 74) mvprintw(y, 54, "%.*s", cols - 56, SLOT_USE[i]);
        }

        int px = cols - 26;
        if (px > 76) {
            attron(A_BOLD);
            mvprintw(4, px, "live preview");
            attroff(A_BOLD);
            mvprintw(5, px, "the console already");
            mvprintw(6, px, "uses what you edit");

            for (int i = 0; i < 8; i++) {
                draw_slot_swatch(8 + i, px, i, 4);
                draw_slot_swatch(8 + i, px + 5, i + 8, 4);
            }

            attron(COLOR_PAIR(1 + 2));
            mvprintw(18, px, "root");
            attroff(COLOR_PAIR(1 + 2));
            printw(":");
            attron(COLOR_PAIR(1 + 4) | A_BOLD);
            printw("~");
            attroff(COLOR_PAIR(1 + 4) | A_BOLD);
            printw("# ls");
            attron(A_REVERSE);
            mvprintw(19, px, " reversed ");
            attroff(A_REVERSE);
        }

        move(help_top, 0);
        for (int x = 0; x < cols; x++) addch('-');

        {
            uint32_t c = *slot_ptr(&t, sel);
            static const char *CH = "RGB";
            mvprintw(help_top + 1, 2, "editing  %s  #%06X   channel %c = %d",
                     SLOT_NAME[sel], c, CH[chan],
                     (int)((c >> ((2 - chan) * 8)) & 0xFF));
            mvprintw(help_top + 2, 2, "%s", SLOT_USE[sel]);

            mvprintw(help_top + 4, 2,
                     "up down  pick colour     left right  pick channel");
            mvprintw(help_top + 5, 2,
                     "-  +     change by 1     _  =        change by 16");
            mvprintw(help_top + 4, 56,
                     "h  type a value like #1E1E2E");
            mvprintw(help_top + 5, 56,
                     "n  name it     s  save     q  quit");
        }

        attron(A_REVERSE);
        move(rows - 1, 0);
        for (int x = 0; x < cols - 1; x++) addch(' ');
        mvprintw(rows - 1, 2,
                 "changes show on screen at once; nothing is written until you press s");
        attroff(A_REVERSE);
        refresh();

        int ch = getch();
        uint32_t *c = slot_ptr(&t, sel);
        int shift = (2 - chan) * 8;
        int val = (int)((*c >> shift) & 0xFF);

        if (ch == KEY_UP)         { if (sel > 0) sel--; }
        else if (ch == KEY_DOWN)  { if (sel < 17) sel++; }
        else if (ch == KEY_LEFT)  { if (chan > 0) chan--; }
        else if (ch == KEY_RIGHT) { if (chan < 2) chan++; }
        else if (ch == '-' || ch == '_') {
            val -= (ch == '_') ? 16 : 1;
            if (val < 0) val = 0;
            *c = (*c & ~(0xFFu << shift)) | ((uint32_t)val << shift);
            dirty = 1; live = t; theme_apply_live(&live);
        }
        else if (ch == '+' || ch == '=') {
            val += (ch == '+') ? 16 : 1;
            if (val > 255) val = 255;
            *c = (*c & ~(0xFFu << shift)) | ((uint32_t)val << shift);
            dirty = 1; live = t; theme_apply_live(&live);
        }
        else if (ch == '\n' || ch == '\r' || ch == KEY_ENTER ||
                 (ch >= '0' && ch <= '9')) {
            char first[2] = { 0, 0 };
            if (ch >= '0' && ch <= '9') first[0] = (char)ch;
            char buf[8];
            static const char *CHNAME[3] = { "red", "green", "blue" };
            char label[48];
            snprintf(label, sizeof label, "%s of %s (0-255):",
                     CHNAME[chan], SLOT_NAME[sel]);
            if (prompt_seeded(label, buf, sizeof buf, first) > 0) {
                int v = atoi(buf);
                if (v < 0) v = 0;
                if (v > 255) v = 255;
                *c = (*c & ~(0xFFu << shift)) | ((uint32_t)v << shift);
                dirty = 1; live = t; theme_apply_live(&live);
            }
        }
        else if (ch == 'h' || ch == 'H') {
            char buf[16];
            if (prompt_line("colour #RRGGBB:", buf, sizeof buf) > 0) {
                uint32_t v;
                if (parse_hex(buf, &v) == 0) {
                    *c = v; dirty = 1; live = t; theme_apply_live(&live);
                }
            }
        }
        else if (ch == 'n' || ch == 'N') {
            char buf[32];
            if (prompt_line("theme name:", buf, sizeof buf) > 0) {
                snprintf(tname, sizeof tname, "%s", buf);
                dirty = 1;
            }
        }
        else if (ch == 's' || ch == 'S') {
            char buf[64];
            if (prompt_line("description:", buf, sizeof buf) >= 0) {
                if (buf[0]) snprintf(about, sizeof about, "%s", buf);
                if (save_theme(tname, &t, about) == 0) dirty = 0;
            }
        }
        else if (ch == 'q' || ch == 'Q' || ch == 27) break;
    }

    endwin();
    if (dirty)
        printf("theme: %s was not saved\n", tname);
    else
        printf("theme: saved as %s; run 'theme %s' to use it\n", tname, tname);
    return 0;
}
