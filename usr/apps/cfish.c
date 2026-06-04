#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <dirent.h>
#include <termios.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/cervus.h>
#include <cervus_util.h>

// Константы
#ifndef VFS_MAX_PATH
#define VFS_MAX_PATH 512
#endif

#define MAX_ARGS 32
#define MAX_HISTORY 50
#define MAX_Z_DIRS 50
#define MAX_CMD_LEN 256

// Структуры
typedef struct {
    char path[VFS_MAX_PATH];
    int count;
} z_dir_t;

// Глобальные переменные
static char cwd[VFS_MAX_PATH];
static int last_rc = 0;
static char history[MAX_HISTORY][MAX_CMD_LEN];
static int hist_cnt = 0;
static z_dir_t z_db[MAX_Z_DIRS];
static int z_cnt = 0;

// --- ТЕРМИНАЛ ---
static void raw_mode(int enable) {
    struct termios t;
    if (tcgetattr(0, &t) < 0) return;
    if (enable) t.c_lflag &= ~(ICANON | ECHO | ISIG);
    else t.c_lflag |= (ICANON | ECHO | ISIG);
    tcsetattr(0, TCSANOW, &t);
}

// --- УТИЛИТЫ ---
static int is_executable(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return (st.st_type == 0); // В Cervus 0 - файл, 1 - папка
}

static void track_dir(const char *path) {
    if (!path || path[0] == '\0') return;
    for (int i = 0; i < z_cnt; i++) {
        if (strcmp(z_db[i].path, path) == 0) {
            z_db[i].count++;
            return;
        }
    }
    if (z_cnt < MAX_Z_DIRS) {
        strncpy(z_db[z_cnt].path, path, VFS_MAX_PATH - 1);
        z_db[z_cnt].count = 1;
        z_cnt++;
    }
}

static int tokenize(char *line, char **argv) {
    int argc = 0;
    char *p = line;
    while (*p && argc < MAX_ARGS - 1) {
        while (isspace((unsigned char)*p)) p++;
        if (!*p) break;
        argv[argc++] = p;
        while (*p && !isspace((unsigned char)*p)) p++;
        if (*p) *p++ = '\0';
    }
    argv[argc] = NULL;
    return argc;
}

// --- АВТОДОПОЛНЕНИЕ ---
static void do_complete(char *buf, int *len, int *pos) {
    char *last_word = strrchr(buf, ' ');
    last_word = last_word ? last_word + 1 : buf;
    int lw_len = strlen(last_word);
    if (lw_len == 0) return;

    const char *scan_paths[] = {"/bin", "/apps", "/usr/bin", NULL};
    for (int i = 0; scan_paths[i]; i++) {
        DIR *d = opendir(scan_paths[i]);
        if (!d) continue;
        struct dirent *de;
        while ((de = readdir(d))) {
            if (strncmp(de->d_name, last_word, lw_len) == 0) {
                int to_add = strlen(de->d_name) - lw_len;
                if (*len + to_add < MAX_CMD_LEN - 2) {
                    strcat(buf, de->d_name + lw_len);
                    strcat(buf, " ");
                    *len = strlen(buf);
                    *pos = *len;
                    closedir(d);
                    return;
                }
            }
        }
        closedir(d);
    }
}

// --- ОТРИСОВКА ---
static void refresh_line(const char *buf, int pos) {
    printf("\r\x1b[K"); // Очистить строку
    // Prompt
    printf("%s %s%s%s > ", 
        last_rc == 0 ? C_GREEN "✔" : C_RED "✘",
        C_BLUE, strncmp(cwd, "/home", 5) == 0 ? "~" : cwd, C_RESET);
    
    // Простая подсветка (команда желтая)
    printf("%s%s%s", C_YELLOW, buf, C_RESET);
    
    // Вернуть курсор на место
    int move = strlen(buf) - pos;
    if (move > 0) printf("\x1b[%dD", move);
}

// --- ИСПОЛНЕНИЕ ---
static int execute_command(int argc, char **argv) {
    if (argc == 0) return 0;

    if (strcmp(argv[0], "cd") == 0) {
        char next[VFS_MAX_PATH];
        resolve_path(cwd, argv[1] ? argv[1] : "/", next, VFS_MAX_PATH);
        struct stat st;
        if (stat(next, &st) == 0 && st.st_type == 1) {
            chdir(next);
            strncpy(cwd, next, VFS_MAX_PATH - 1);
            track_dir(cwd);
            return 0;
        }
        printf(C_RED "cd: %s is not a directory\n" C_RESET, next);
        return 1;
    }

    if (strcmp(argv[0], "exit") == 0) exit(0);

    // Поиск внешней программы
    char binpath[VFS_MAX_PATH];
    int found = 0;
    if (argv[0][0] == '/' || argv[0][0] == '.') {
        resolve_path(cwd, argv[0], binpath, VFS_MAX_PATH);
        if (is_executable(binpath)) found = 1;
    } else {
        const char *search[] = {"/bin/", "/apps/", "/usr/bin/", NULL};
        for (int i = 0; search[i]; i++) {
            snprintf(binpath, VFS_MAX_PATH, "%s%s", search[i], argv[0]);
            if (is_executable(binpath)) { found = 1; break; }
        }
    }

    if (!found) {
        printf(C_RED "c_fish: command not found: %s\n" C_RESET, argv[0]);
        return 127;
    }

    // Запуск
    pid_t pid = fork();
    if (pid == 0) {
        char cwd_flag[VFS_MAX_PATH + 10];
        snprintf(cwd_flag, sizeof(cwd_flag), "--cwd=%s", cwd);
        char *real_argv[MAX_ARGS + 2];
        int i;
        for (i = 0; i < argc; i++) real_argv[i] = argv[i];
        real_argv[i++] = cwd_flag;
        real_argv[i] = NULL;

        execve(binpath, real_argv, NULL);
        exit(127);
    } else if (pid > 0) {
        int status;
        waitpid(pid, &status, 0);
        return WEXITSTATUS(status);
    }
    return 1;
}

// --- MAIN ---
int main(int argc, char **argv) {
    if (getcwd(cwd, VFS_MAX_PATH) == NULL) strcpy(cwd, "/");
    
    raw_mode(1);
    printf(C_CYAN "c_fish v0.1: [TAB] completion, [Arrows] history, [✔/✘] status\n" C_RESET);

    char line[MAX_CMD_LEN] = {0};
    int len = 0, pos = 0, h_idx = -1;

    while (1) {
        refresh_line(line, pos);
        unsigned char c;
        if (read(0, &c, 1) <= 0) break;

        if (c == '\n' || c == '\r') {
            raw_mode(0); printf("\n");
            if (len > 0) {
                if (hist_cnt < MAX_HISTORY) strcpy(history[hist_cnt++], line);
                char *args[MAX_ARGS];
                char work_line[MAX_CMD_LEN];
                strcpy(work_line, line);
                int ac = tokenize(work_line, args);
                last_rc = execute_command(ac, args);
            }
            line[0] = 0; len = 0; pos = 0; h_idx = hist_cnt;
            raw_mode(1);
        } else if (c == '\t') {
            do_complete(line, &len, &pos);
        } else if (c == 127 || c == '\b') {
            if (pos > 0) {
                memmove(line + pos - 1, line + pos, len - pos + 1);
                len--; pos--;
            }
        } else if (c == 27) { // ESC / Arrows
            char seq[2];
            if (read(0, &seq[0], 1) > 0 && read(0, &seq[1], 1) > 0) {
                if (seq[1] == 'A' && hist_cnt > 0) { // UP
                    if (h_idx > 0) h_idx--;
                    if (h_idx >= 0) { strcpy(line, history[h_idx]); len = pos = strlen(line); }
                } else if (seq[1] == 'B') { // DOWN
                    if (h_idx < hist_cnt - 1) { h_idx++; strcpy(line, history[h_idx]); }
                    else { h_idx = hist_cnt; line[0] = 0; }
                    len = pos = strlen(line);
                } else if (seq[1] == 'C' && pos < len) pos++;
                else if (seq[1] == 'D' && pos > 0) pos--;
            }
        } else if (c >= 32 && c < 127 && len < MAX_CMD_LEN - 5) {
            memmove(line + pos + 1, line + pos, len - pos + 1);
            line[pos++] = c; len++;
        }
    }
    raw_mode(0);
    return 0;
}
