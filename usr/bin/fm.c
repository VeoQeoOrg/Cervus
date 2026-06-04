#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <termios.h>
#include <fcntl.h>
#include <cervus_util.h>

#define MAX_FILES 128
#define UI_HEIGHT 20
#define LEFT_PANEL_WIDTH 35

#ifndef C_BG_BLUE
#define C_BG_BLUE "\x1b[44m"
#endif
#ifndef C_WHITE
#define C_WHITE   "\x1b[37m"
#endif

typedef struct {
    char name[256];
    int is_dir;
    unsigned long size;
} file_entry_t;

static file_entry_t entries[MAX_FILES];
static int file_count = 0;
static int cursor_idx = 0;
static int scroll_offset = 0;
static char current_path[512];

struct termios orig_termios;

void raw_mode(int enable) {
    if (enable) {
        tcgetattr(0, &orig_termios);
        struct termios raw = orig_termios;
        raw.c_lflag &= ~(ICANON | ECHO | ISIG);
        tcsetattr(0, TCSANOW, &raw);
    } else {
        tcsetattr(0, TCSANOW, &orig_termios);
    }
}

void load_dir(const char *path) {
    DIR *d = opendir(path);
    if (!d) return;
    file_count = 0;
    struct dirent *de;
    while ((de = readdir(d)) != NULL && file_count < MAX_FILES) {
        if (strcmp(de->d_name, ".") == 0) continue;
        
        strncpy(entries[file_count].name, de->d_name, 255);
        entries[file_count].name[255] = '\0';
        
        char full[1024];
        snprintf(full, sizeof(full), "%s/%s", path, de->d_name);
        struct stat st;
        if (stat(full, &st) == 0) {
            entries[file_count].is_dir = (st.st_type == 1);
            entries[file_count].size = (unsigned long)st.st_size;
        } else {
            entries[file_count].is_dir = 0; 
            entries[file_count].size = 0;
        }
        file_count++;
    }
    closedir(d);

    for (int i = 0; i < file_count - 1; i++) {
        for (int j = 0; j < file_count - i - 1; j++) {
            if (!entries[j].is_dir && entries[j+1].is_dir) {
                file_entry_t tmp = entries[j]; 
                entries[j] = entries[j+1]; 
                entries[j+1] = tmp;
            }
        }
    }
}

void draw_preview(int start_y) {
    if (file_count == 0 || entries[cursor_idx].is_dir) {
        fputs("\x1b[m", stdout);
        for(int i=0; i<UI_HEIGHT; i++) {
            printf("\x1b[%d;%dH| %-40s", start_y + i, LEFT_PANEL_WIDTH + 2, 
                   i == 5 ? (entries[cursor_idx].is_dir ? "<DIRECTORY>" : "") : "");
        }
        return;
    }

    char full[1024];
    snprintf(full, sizeof(full), "%s/%s", current_path, entries[cursor_idx].name);
    int fd = open(full, O_RDONLY, 0);
    if (fd < 0) return;

    char buf[128];
    for (int i = 0; i < UI_HEIGHT; i++) {
        memset(buf, 0, sizeof(buf));
        int n = read(fd, buf, 40); 
        if (n <= 0) {
            printf("\x1b[%d;%dH| %-40s", start_y + i, LEFT_PANEL_WIDTH + 2, "");
            continue;
        }
        for(int j=0; j<n; j++) {
            if(buf[j] == '\n' || buf[j] == '\r') {
                buf[j] = ' ';
            }
        }
        buf[n] = '\0';
        printf("\x1b[%d;%dH| " C_GRAY "%-40s" C_RESET, start_y + i, LEFT_PANEL_WIDTH + 2, buf);
    }
    close(fd);
}

void draw_ui() {
    fputs("\x1b[2J\x1b[H\x1b[?25l", stdout);
    printf(C_BOLD C_CYAN " Cervus Explorer " C_RESET);
    printf(C_GRAY " | %s\n" C_RESET, current_path);
    fputs(C_GRAY " ----------------------------------------------------------------------------\n" C_RESET, stdout);

    int end = scroll_offset + UI_HEIGHT;
    if (end > file_count) end = file_count;

    for (int i = scroll_offset; i < scroll_offset + UI_HEIGHT; i++) {
        int row_y = 3 + (i - scroll_offset);
        printf("\x1b[%d;1H", row_y);

        if (i < file_count) {
            if (i == cursor_idx) fputs(C_BG_BLUE C_WHITE, stdout);
            
            char name_trunc[LEFT_PANEL_WIDTH];
            strncpy(name_trunc, entries[i].name, LEFT_PANEL_WIDTH-1);
            name_trunc[LEFT_PANEL_WIDTH-1] = '\0';

            printf(" %-2s %-30s " C_RESET, entries[i].is_dir ? "D " : "  ", name_trunc);
        } else {
            printf("%-35s", "");
        }
    }

    draw_preview(3);

    fputs("\x1b[24;1H", stdout);
    fputs(C_GRAY " ----------------------------------------------------------------------------\n" C_RESET, stdout);
    printf(C_BOLD " [Arrows]" C_RESET " Nav  " C_BOLD "[Enter]" C_RESET " Open  " C_BOLD "[BS]" C_RESET " Up  " C_BOLD "[Q]" C_RESET " Exit\n");
    fflush(stdout);
}

int main(int argc, char **argv) {
    if (getcwd(current_path, 511) == NULL) strcpy(current_path, "/");
    current_path[511] = '\0';
    load_dir(current_path);
    raw_mode(1);

    while (1) {
        draw_ui();
        char c;
        if (read(0, &c, 1) <= 0) continue;
        if (c == 'q' || c == 'Q') break;
        if (c == '\x1b') {
            char seq[2];
            if (read(0, &seq[0], 1) > 0 && read(0, &seq[1], 1) > 0) {
                if (seq[1] == 'A' && cursor_idx > 0) { 
                    cursor_idx--;
                    if (cursor_idx < scroll_offset) scroll_offset = cursor_idx;
                }
                if (seq[1] == 'B' && cursor_idx < file_count - 1) { 
                    cursor_idx++;
                    if (cursor_idx >= scroll_offset + UI_HEIGHT) scroll_offset++;
                }
            }
        }
        if (c == '\n' || c == '\r') {
            if (file_count > 0 && entries[cursor_idx].is_dir) {
                char temp_path[512];
                if (strcmp(current_path, "/") == 0) {
                    snprintf(temp_path, sizeof(temp_path), "/%s", entries[cursor_idx].name);
                } else {
                    snprintf(temp_path, sizeof(temp_path), "%s/%s", current_path, entries[cursor_idx].name);
                }
                strncpy(current_path, temp_path, sizeof(current_path) - 1);
                current_path[sizeof(current_path) - 1] = '\0';
                
                path_norm(current_path);
                load_dir(current_path);
                cursor_idx = 0; 
                scroll_offset = 0;
            }
        }
        if (c == 127 || c == 8) { 
            if (strcmp(current_path, "/") != 0) {
                char *last = strrchr(current_path, '/');
                if (last == current_path) {
                    current_path[1] = '\0'; 
                } else if (last) {
                    *last = '\0';
                }
                path_norm(current_path); 
                load_dir(current_path);
                cursor_idx = 0; 
                scroll_offset = 0;
            }
        }
    }
    raw_mode(0);
    fputs("\x1b[?25h\x1b[2J\x1b[H", stdout);
    return 0;
}
