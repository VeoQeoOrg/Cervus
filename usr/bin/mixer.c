#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/cervus.h>
#include <tui.h>

#define AUDIO_CONF "/etc/audio.conf"

static const char USAGE[] =
    "Usage: mixer                 interactive mixer\n"
    "       mixer status          print current settings\n"
    "       mixer <0-100>         set volume\n"
    "       mixer +N | -N         adjust volume\n"
    "       mixer mute|unmute|toggle\n"
    "       mixer out <n|auto>    select output device\n"
    "       mixer save            persist settings to " AUDIO_CONF "\n"
    "       mixer restore         apply settings from " AUDIO_CONF "\n";

static const char *kind_icon(unsigned kind) {
    switch (kind) {
        case CERVUS_AUDIO_OUT_SPEAKER:   return "spk";
        case CERVUS_AUDIO_OUT_HEADPHONE: return "hp ";
        case CERVUS_AUDIO_OUT_LINEOUT:   return "out";
        case CERVUS_AUDIO_OUT_DIGITAL:   return "dig";
        default:                         return "   ";
    }
}

static int load_mixer(cervus_audio_mixer_t *m) {
    if (cervus_audio_mixer_get(m) != 0) {
        fprintf(stderr, "mixer: no audio device\n");
        return -1;
    }
    return 0;
}

static int save_config(void) {
    cervus_audio_mixer_t m;
    if (load_mixer(&m) != 0) return 1;
    FILE *f = fopen(AUDIO_CONF, "w");
    if (!f) { fprintf(stderr, "mixer: cannot write %s\n", AUDIO_CONF); return 1; }
    fprintf(f, "volume=%d\n", m.volume);
    fprintf(f, "mute=%d\n", m.mute);
    if (m.current < 0) fprintf(f, "output=auto\n");
    else               fprintf(f, "output=%d\n", m.current);
    fclose(f);
    return 0;
}

static int restore_config(int quiet) {
    FILE *f = fopen("/mnt" AUDIO_CONF, "r");
    if (!f) f = fopen(AUDIO_CONF, "r");
    if (!f) {
        if (!quiet) fprintf(stderr, "mixer: no %s\n", AUDIO_CONF);
        return 1;
    }
    char line[128];
    while (fgets(line, sizeof line, f)) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = 0;
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        const char *val = eq + 1;
        if      (!strcmp(line, "volume")) cervus_audio_set_volume(atoi(val));
        else if (!strcmp(line, "mute"))   cervus_audio_set_mute(atoi(val));
        else if (!strcmp(line, "output")) {
            if (!strcmp(val, "auto")) cervus_audio_set_output(-1);
            else                      cervus_audio_set_output(atoi(val));
        }
    }
    fclose(f);
    return 0;
}

static void print_status(void) {
    cervus_audio_mixer_t m;
    if (load_mixer(&m) != 0) return;
    printf("driver:  %s\n", m.driver);
    printf("volume:  %d%%%s\n", m.volume, m.mute ? " (muted)" : "");
    printf("outputs:\n");
    for (int i = 0; i < m.noutputs; i++) {
        int cur = (m.current < 0) ? 0 : (i == m.current);
        printf("  %c %d  %s %-12s %s\n",
               cur ? '*' : ' ', i, kind_icon(m.outputs[i].kind),
               m.outputs[i].name,
               m.outputs[i].present ? "connected" : "not connected");
    }
    if (m.current < 0) printf("  (auto)\n");
}

static void draw_bar(int row, int col, int width, int pct, int mute) {
    char buf[256];
    int fill = width * pct / 100;
    if (fill > width) fill = width;
    int n = 0;
    buf[n++] = '[';
    for (int i = 0; i < width && n < (int)sizeof buf - 8; i++)
        buf[n++] = (i < fill) ? '#' : '-';
    buf[n++] = ']';
    buf[n] = 0;
    tui_move(row, col);
    printf("%s%s %3d%%%s ", mute ? "\x1b[90m" : "\x1b[32m", buf, pct, "\x1b[0m");
    if (mute) printf("\x1b[91mMUTE\x1b[0m");
    else      printf("    ");
}

static void interactive(void) {
    cervus_audio_mixer_t m;
    if (load_mixer(&m) != 0) return;

    tui_begin();
    tui_hide_cursor();
    int sel = 0;
    int running = 1;
    int dirty = 0;

    while (running) {
        cervus_audio_mixer_get(&m);
        int rows, cols;
        tui_size(&rows, &cols);
        (void)rows;
        tui_clear();

        tui_move(1, 1);
        printf("\x1b[1m Cervus mixer \x1b[0m  driver: %s%s", m.driver,
               dirty ? "   \x1b[93m(unsaved)\x1b[0m" : "");

        tui_move(3, 2);
        printf("Volume");
        int bw = cols - 24;
        if (bw < 10) bw = 10;
        if (bw > 60) bw = 60;
        draw_bar(4, 2, bw, m.volume, m.mute);

        tui_move(6, 2);
        printf("\x1b[1mOutput device\x1b[0m");
        for (int i = 0; i < m.noutputs; i++) {
            int active = (m.current < 0) ? 0 : (i == m.current);
            tui_move(7 + i, 2);
            if (i == sel) printf("\x1b[7m");
            printf(" %c %s %-12s %-14s",
                   active ? '*' : ' ',
                   kind_icon(m.outputs[i].kind),
                   m.outputs[i].name,
                   m.outputs[i].present ? "connected" : "not connected");
            if (i == sel) printf("\x1b[0m");
        }
        int arow = 7 + m.noutputs;
        tui_move(arow, 2);
        if (sel == m.noutputs) printf("\x1b[7m");
        printf(" %c     %-12s %-14s", m.current < 0 ? '*' : ' ', "Auto", "follow jack");
        if (sel == m.noutputs) printf("\x1b[0m");

        tui_move(arow + 2, 2);
        printf("\x1b[90m<- -> volume   up/down select   Enter apply   m mute   s save   q quit\x1b[0m");
        fflush(stdout);

        int k = tui_read_key();
        switch (k) {
            case TK_LEFT:  cervus_audio_set_volume(m.volume - 5); dirty = 1; break;
            case TK_RIGHT: cervus_audio_set_volume(m.volume + 5); dirty = 1; break;
            case TK_UP:    if (sel > 0) sel--; break;
            case TK_DOWN:  if (sel < m.noutputs) sel++; break;
            case TK_ENTER:
                cervus_audio_set_output(sel == m.noutputs ? -1 : sel);
                dirty = 1;
                break;
            case 'm': case 'M':
                cervus_audio_set_mute(!m.mute); dirty = 1; break;
            case 's': case 'S':
                if (save_config() == 0) dirty = 0;
                break;
            case 'q': case 'Q': case TK_ESC:
                running = 0; break;
            default: break;
        }
    }

    tui_show_cursor();
    tui_end();
}

int main(int argc, char **argv) {
    if (argc < 2) { interactive(); return 0; }

    const char *a = argv[1];
    if (!strcmp(a, "-h") || !strcmp(a, "--help")) { fputs(USAGE, stdout); return 0; }
    if (!strcmp(a, "status")) { print_status(); return 0; }
    if (!strcmp(a, "save"))    return save_config();
    if (!strcmp(a, "restore")) return restore_config(0);
    if (!strcmp(a, "--quiet-restore")) return restore_config(1);

    cervus_audio_mixer_t m;
    if (load_mixer(&m) != 0) return 1;

    if (!strcmp(a, "mute"))   return cervus_audio_set_mute(1) ? 1 : 0;
    if (!strcmp(a, "unmute")) return cervus_audio_set_mute(0) ? 1 : 0;
    if (!strcmp(a, "toggle")) return cervus_audio_set_mute(!m.mute) ? 1 : 0;

    if (!strcmp(a, "out")) {
        if (argc < 3) { fputs(USAGE, stderr); return 1; }
        if (!strcmp(argv[2], "auto")) return cervus_audio_set_output(-1) ? 1 : 0;
        int idx = atoi(argv[2]);
        if (idx < 0 || idx >= m.noutputs) {
            fprintf(stderr, "mixer: no output %s\n", argv[2]);
            return 1;
        }
        return cervus_audio_set_output(idx) ? 1 : 0;
    }

    if (a[0] == '+' || a[0] == '-') {
        int d = atoi(a);
        if (cervus_audio_set_volume(m.volume + d) != 0) return 1;
        print_status();
        return 0;
    }
    if (a[0] >= '0' && a[0] <= '9') {
        if (cervus_audio_set_volume(atoi(a)) != 0) return 1;
        print_status();
        return 0;
    }

    fputs(USAGE, stderr);
    return 1;
}
