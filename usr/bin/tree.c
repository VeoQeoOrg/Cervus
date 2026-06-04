#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cervus_util.h>

// Цвета из твоего cervus_util.h
#define COLOR_DIR   C_BLUE
#define COLOR_OFF   C_RESET

static int dir_count = 0;
static int file_count = 0;

// Функция для отрисовки дерева
void walk_tree(const char *root_path, int level, char *prefix) {
    DIR *d = opendir(root_path);
    if (!d) return;

    struct dirent *de;
    struct dirent *entries[128];
    int count = 0;

    // Читаем все записи, чтобы знать, какая из них последняя
    while ((de = readdir(d)) != NULL && count < 128) {
        // Игнорируем . и ..
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
            continue;
        
        // Копируем dirent, так как readdir возвращает указатель на статический буфер
        entries[count] = malloc(sizeof(struct dirent));
        memcpy(entries[count], de, sizeof(struct dirent));
        count++;
    }
    closedir(d);

    for (int i = 0; i < count; i++) {
        int is_last = (i == count - 1);
        
        // Рисуем префикс и ветку
        printf("%s%s%s", prefix, is_last ? "└── " : "├── ", "");

        // Проверяем, папка это или файл через stat
        char full_path[512];
        snprintf(full_path, sizeof(full_path), "%s/%s", root_path, entries[i]->d_name);
        
        struct stat st;
        int is_dir = 0;
        if (stat(full_path, &st) == 0) {
            if (st.st_type == 1) is_dir = 1; // В Cervus 1 - это папка
        }

        if (is_dir) {
            printf("%s%s%s\n", COLOR_DIR, entries[i]->d_name, COLOR_OFF);
            dir_count++;

            // Подготовка префикса для следующего уровня
            char new_prefix[512];
            snprintf(new_prefix, sizeof(new_prefix), "%s%s", prefix, is_last ? "    " : "│   ");
            
            walk_tree(full_path, level + 1, new_prefix);
        } else {
            printf("%s\n", entries[i]->d_name);
            file_count++;
        }
        free(entries[i]);
    }
}

int main(int argc, char **argv) {
    const char *start_node = ".";
    char cwd_str_buf[512];
    const char *cwd_str = getcwd(cwd_str_buf, sizeof(cwd_str_buf)) ? cwd_str_buf : "/";
    // Если передан аргумент, используем его как корень дерева
    for (int i = 1; i < argc; i++) {

        start_node = argv[i];
        break;
    }

    char resolved_root[512];
    resolve_path(cwd_str, start_node, resolved_root, sizeof(resolved_root));

    struct stat st;
    if (stat(resolved_root, &st) != 0 || st.st_type != 1) {
        printf("tree: %s: [error or not a directory]\n", start_node);
        return 1;
    }

    printf("%s%s%s\n", COLOR_DIR, start_node, COLOR_OFF);
    
    char prefix[512] = "";
    walk_tree(resolved_root, 0, prefix);

    printf("\n%d directories, %d files\n", dir_count, file_count);

    return 0;
}

