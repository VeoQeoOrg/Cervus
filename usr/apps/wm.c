#include <stdio.h>
#include <unistd.h>
#include <termios.h>
#include <libgui.h>

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    
    if (gui_init() != 0) {
        printf("Failed to init GUI (no framebuffer)\n");
        return 1;
    }
    
    struct termios orig, raw;
    int have_tio = (tcgetattr(0, &orig) == 0);
    if (have_tio) {
        raw = orig;
        raw.c_lflag &= ~(ECHO | ICANON | ISIG);
        raw.c_cc[VMIN]  = 1;
        raw.c_cc[VTIME] = 0;
        tcsetattr(0, TCSAFLUSH, &raw);
    }
    
    gui_window_t win1 = {100, 100, 400, 300, 0xDDDDDD, 0x336699, "Window 1", 1};
    gui_window_t win2 = {150, 150, 300, 200, 0xEEEEEE, 0x993333, "Window 2", 0};
    
    int active_win = 1; // 1 or 2
    int running = 1;
    
    while (running) {
        gui_clear(0x222233); // Desktop background
        
        // Draw inactive window first, then active
        if (active_win == 1) {
            win2.is_active = 0;
            win1.is_active = 1;
            gui_draw_window(&win2);
            gui_draw_window(&win1);
        } else {
            win1.is_active = 0;
            win2.is_active = 1;
            gui_draw_window(&win1);
            gui_draw_window(&win2);
        }
        
        gui_flush();
        
        // Handle input (keyboard) to move windows
        char c;
        if (read(0, &c, 1) > 0) {
            if (c == 'q' || c == 'Q') {
                running = 0;
            } else if (c == '\t') {
                active_win = (active_win == 1) ? 2 : 1;
            } else if (c == 27) { // ESC sequence for arrows
                char seq[2];
                if (read(0, &seq[0], 1) > 0 && read(0, &seq[1], 1) > 0) {
                    gui_window_t *w = (active_win == 1) ? &win1 : &win2;
                    if (seq[1] == 'A') w->y -= 20; // Up
                    if (seq[1] == 'B') w->y += 20; // Down
                    if (seq[1] == 'C') w->x += 20; // Right
                    if (seq[1] == 'D') w->x -= 20; // Left
                }
            } else if (c == 'w') {
                gui_window_t *w = (active_win == 1) ? &win1 : &win2;
                w->y -= 20;
            } else if (c == 's') {
                gui_window_t *w = (active_win == 1) ? &win1 : &win2;
                w->y += 20;
            } else if (c == 'a') {
                gui_window_t *w = (active_win == 1) ? &win1 : &win2;
                w->x -= 20;
            } else if (c == 'd') {
                gui_window_t *w = (active_win == 1) ? &win1 : &win2;
                w->x += 20;
            }
        }
    }
    
    gui_deinit();
    if (have_tio) tcsetattr(0, TCSAFLUSH, &orig);
    return 0;
}
