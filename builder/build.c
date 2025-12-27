#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <time.h>
#include <errno.h>
#include <limits.h>

#define IMAGE_NAME "Cervus"
#define VERSION "v0.0.1"
#define QEMUFLAGS "-m 2G"

#define COLOR_RESET   "\033[0m"
#define COLOR_RED     "\033[91m"
#define COLOR_GREEN   "\033[92m"
#define COLOR_YELLOW  "\033[93m"
#define COLOR_BLUE    "\033[94m"
#define COLOR_MAGENTA "\033[95m"
#define COLOR_CYAN    "\033[96m"
#define COLOR_BOLD    "\033[1m"

const char *DIRS_TO_CLEAN[] = { "bin", "obj", "iso_root", "limine", "kernel/linker-scripts", "demo_iso", "limine-tools", "edk2-ovmf", NULL };
const char *FILES_TO_CLEAN[] = { "Cervus.iso", "Cervus.hdd", "kernel/.deps-obtained", "limine.conf", "OS-TREE.txt", NULL };

const char *SSE_FILES[] = {
    "sse.c", "fpu.c", "printf.c", "fabs.c", "pow.c", "pow10.c", "serial.c", "pmm.c", NULL
};

struct Dependency {
    const char *name;
    const char *url;
    const char *commit;
};

struct Dependency DEPENDENCIES[] = {
    {"freestnd-c-hdrs", "https://codeberg.org/OSDev/freestnd-c-hdrs-0bsd.git", "5df91dd7062ad0c54f5ffd86193bb9f008677631"},
    {"cc-runtime", "https://codeberg.org/OSDev/cc-runtime.git", "dae79833b57a01b9fd3e359ee31def69f5ae899b"},
    {"limine-protocol", "https://codeberg.org/Limine/limine-protocol.git", "c4616df2572d77c60020bdefa617dd9bdcc6566a"},
    {NULL, NULL, NULL}
};

bool ARG_NO_CLEAN = false;
bool ARG_TREE = false;
bool ARG_STRUCTURE_ONLY = false;

char **TREE_FILES = NULL;
int TREE_FILES_COUNT = 0;
int TREE_FILES_CAPACITY = 0;

void print_color(const char *color, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    printf("%s", color);
    vprintf(fmt, args);
    printf("%s\n", COLOR_RESET);
    va_end(args);
}

void add_tree_file(const char *filename) {
    if (TREE_FILES_COUNT >= TREE_FILES_CAPACITY) {
        TREE_FILES_CAPACITY = (TREE_FILES_CAPACITY == 0) ? 8 : TREE_FILES_CAPACITY * 2;
        TREE_FILES = realloc(TREE_FILES, TREE_FILES_CAPACITY * sizeof(char*));
    }
    TREE_FILES[TREE_FILES_COUNT++] = strdup(filename);
}

bool should_print_content(const char *filename) {
    if (TREE_FILES_COUNT == 0) return true;
    
    for (int i = 0; i < TREE_FILES_COUNT; i++) {
        if (strcmp(filename, TREE_FILES[i]) == 0) return true;
    }
    return false;
}

int cmd_run(bool capture, const char *fmt, ...) {
    char cmd[4096];
    va_list args;
    va_start(args, fmt);
    vsnprintf(cmd, sizeof(cmd), fmt, args);
    va_end(args);

    if (capture) {
        print_color(COLOR_BLUE, "Running: %s", cmd);
    }
    
    int ret = system(cmd);
    if (ret != 0) {
        return WEXITSTATUS(ret); 
    }
    return 0;
}

void ensure_dir(const char *path) {
    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "mkdir -p %s", path);
    system(tmp);
}

bool file_exists(const char *path) {
    return access(path, F_OK) == 0;
}

time_t get_mtime(const char *path) {
    struct stat attr;
    if (stat(path, &attr) == 0) return attr.st_mtime;
    return 0;
}

void rm_rf(const char *path) {
    if (file_exists(path)) {
        print_color(COLOR_BLUE, "Removing %s", path);
        cmd_run(false, "rm -rf %s", path);
    }
}

bool setup_dependencies() {
    print_color(COLOR_GREEN, "Checking dependencies...");
    ensure_dir("limine-tools");

    for (int i = 0; DEPENDENCIES[i].name != NULL; i++) {
        char path[256];
        snprintf(path, sizeof(path), "limine-tools/%s", DEPENDENCIES[i].name);
        
        if (!file_exists(path)) {
            print_color(COLOR_YELLOW, "Missing %s, setting up...", DEPENDENCIES[i].name);
            if (cmd_run(true, "git clone %s %s", DEPENDENCIES[i].url, path) != 0) return false;
            
            char git_cmd[512];
            snprintf(git_cmd, sizeof(git_cmd), "git -C %s -c advice.detachedHead=false checkout %s", path, DEPENDENCIES[i].commit);
            if (system(git_cmd) != 0) return false;
        }
    }
    return true;
}

bool build_limine() {
    if (file_exists("limine/limine")) {
        print_color(COLOR_GREEN, "Limine already built");
        return true;
    }
    
    print_color(COLOR_GREEN, "Building Limine...");
    if (file_exists("limine")) rm_rf("limine");
    
    if (cmd_run(true, "git clone https://codeberg.org/Limine/Limine.git limine --branch=v10.5.0-binary --depth=1") != 0) return false;
    if (cmd_run(true, "make -C limine") != 0) return false;
    
    return true;
}

bool is_sse_file(const char *filename) {
    for (int i = 0; SSE_FILES[i] != NULL; i++) {
        if (strstr(filename, SSE_FILES[i]) != NULL) return true;
    }
    return false;
}

void ensure_linker_script() {
    ensure_dir("kernel/linker-scripts");
    const char *lds_path = "kernel/linker-scripts/x86_64.lds";
    if (file_exists(lds_path)) return;

    FILE *f = fopen(lds_path, "w");
    if (!f) return;
    fprintf(f, "OUTPUT_FORMAT(elf64-x86-64)\nENTRY(kernel_main)\nPHDRS\n{\n    limine_requests PT_LOAD;\n    text PT_LOAD;\n    rodata PT_LOAD;\n    data PT_LOAD;\n}\nSECTIONS\n{\n    . = 0xffffffff80000000;\n    .limine_requests : {\n        KEEP(*(.limine_requests_start))\n        KEEP(*(.limine_requests))\n        KEEP(*(.limine_requests_end))\n    } :limine_requests\n    . = ALIGN(CONSTANT(MAXPAGESIZE));\n    .text : { *(.text .text.*) } :text\n    . = ALIGN(CONSTANT(MAXPAGESIZE));\n    .rodata : { *(.rodata .rodata.*) } :rodata\n    .note.gnu.build-id : { *(.note.gnu.build-id) } :rodata\n    . = ALIGN(CONSTANT(MAXPAGESIZE));\n    .data : { *(.data .data.*) } :data\n    .bss : { *(.bss .bss.*) *(COMMON) } :data\n    /DISCARD/ : { *(.eh_frame*) *(.note .note.*) }\n}\n");
    fclose(f);
    print_color(COLOR_GREEN, "x86_64.lds created");
}

typedef struct {
    char **paths;
    int count;
    int capacity;
} FileList;

void file_list_add(FileList *list, const char *path) {
    if (list->count >= list->capacity) {
        list->capacity = list->capacity == 0 ? 16 : list->capacity * 2;
        list->paths = realloc(list->paths, list->capacity * sizeof(char*));
    }
    list->paths[list->count++] = strdup(path);
}

void find_src_files(const char *root_dir, FileList *list) {
    DIR *dir;
    struct dirent *entry;

    if (!(dir = opendir(root_dir))) return;

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_DIR) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 || 
                strcmp(entry->d_name, ".git") == 0) continue;
            char path[1024];
            snprintf(path, sizeof(path), "%s/%s", root_dir, entry->d_name);
            find_src_files(path, list);
        } else {
            const char *ext = strrchr(entry->d_name, '.');
            if (ext && (strcmp(ext, ".c") == 0 || strcmp(ext, ".asm") == 0 || 
                        strcmp(ext, ".S") == 0 || strcmp(ext, ".psf") == 0)) {
                char path[1024];
                snprintf(path, sizeof(path), "%s/%s", root_dir, entry->d_name);
                file_list_add(list, path);
            }
        }
    }
    closedir(dir);
}

bool compile_kernel() {
    print_color(COLOR_GREEN, "Compiling kernel...");
    ensure_linker_script();
    if (!setup_dependencies()) return false;
    
    ensure_dir("libc/include");
    ensure_dir("libc/src");
    ensure_dir("bin");
    ensure_dir("obj/kernel");
    ensure_dir("obj/libc");

    const char *base_cflags = "-g -O2 -pipe -Wall -Wextra -std=gnu11 -nostdinc -ffreestanding "
                              "-fno-stack-protector -fno-stack-check -fno-lto -fno-PIC "
                              "-ffunction-sections -fdata-sections "
                              "-m64 -march=x86-64 -mabi=sysv -mcmodel=kernel "
                              "-mno-red-zone -mgeneral-regs-only";
    
    const char *core_cflags = "-mno-sse -mno-sse2 -mno-mmx -mno-3dnow";
    const char *sse_cflags_suffix = "-msse -msse2 -mfpmath=sse -mno-mmx -mno-3dnow";

    char cppflags[2048];
    snprintf(cppflags, sizeof(cppflags), 
             "-I kernel/src -I libc/include -I limine-tools/limine-protocol/include -isystem limine-tools/freestnd-c-hdrs/include -MMD -MP");

    FileList sources = {0};
    if (file_exists("kernel/src")) find_src_files("kernel/src", &sources);
    if (file_exists("libc/src")) find_src_files("libc/src", &sources);

    FileList objects = {0};
    bool compilation_failed = false;

    for (int i = 0; i < sources.count; i++) {
        char *src = sources.paths[i];
        
        char category[16] = "other";
        if (strncmp(src, "kernel/", 7) == 0) strcpy(category, "kernel");
        else if (strncmp(src, "libc/", 5) == 0) strcpy(category, "libc");

        char obj_path[1024];
        char flat_name[1024];
        strcpy(flat_name, src);
        for(int j=0; flat_name[j]; j++) if(flat_name[j] == '/' || flat_name[j] == '.') flat_name[j] = '_';
        
        snprintf(obj_path, sizeof(obj_path), "obj/%s/%s.o", category, flat_name);

        file_list_add(&objects, obj_path);

        if (file_exists(obj_path) && get_mtime(src) <= get_mtime(obj_path)) {
            continue;
        }

        const char *ext = strrchr(src, '.');
        
        if (strcmp(ext, ".psf") == 0) {
            print_color(COLOR_BLUE, "Converting binary: %s", src);
            char temp_path[1024];
            snprintf(temp_path, sizeof(temp_path), "temp_%s", strrchr(src, '/') + 1);
            cmd_run(false, "cp %s %s", src, temp_path);
            
            cmd_run(false, "objcopy -I binary -O elf64-x86-64 -B i386:x86-64 --rename-section .data=.rodata,alloc,load,readonly,data,contents %s %s", temp_path, obj_path);
            remove(temp_path);
            
            char stem[256];
            strcpy(stem, strrchr(src, '/') + 1);
            *strrchr(stem, '.') = '\0';
            
            char redefine_args[1024] = "";
            snprintf(redefine_args, sizeof(redefine_args), 
                "--redefine-sym _binary_temp_%s_psf_start=_binary_%s_psf_start "
                "--redefine-sym _binary_temp_%s_psf_end=_binary_%s_psf_end "
                "--redefine-sym _binary_temp_%s_psf_size=_binary_%s_psf_size",
                stem, stem, stem, stem, stem, stem);
            
            cmd_run(false, "objcopy %s %s", redefine_args, obj_path);

        } else if (strcmp(ext, ".asm") == 0) {
            print_color(COLOR_CYAN, "[asm] %s", src);
            if (cmd_run(false, "nasm -g -F dwarf -f elf64 %s -o %s", src, obj_path) != 0) compilation_failed = true;
        } else {
            bool sse = is_sse_file(src);
            const char *color = sse ? COLOR_MAGENTA : COLOR_CYAN;
            
            char final_flags[1024];
            if (sse) {
                snprintf(final_flags, sizeof(final_flags), 
                         "-g -O2 -pipe -Wall -Wextra -std=gnu11 -nostdinc -ffreestanding "
                         "-fno-stack-protector -fno-stack-check -fno-lto -fno-PIC "
                         "-ffunction-sections -fdata-sections "
                         "-m64 -march=x86-64 -mabi=sysv -mcmodel=kernel "
                         "-mno-red-zone %s", sse_cflags_suffix);
            } else {
                 snprintf(final_flags, sizeof(final_flags), "%s %s", base_cflags, core_cflags);
            }

            print_color(color, "[%s] %s", category, src);
            if (cmd_run(false, "gcc %s %s -c %s -o %s", final_flags, cppflags, src, obj_path) != 0) compilation_failed = true;
        }
    }

    if (compilation_failed) {
        print_color(COLOR_RED, "Compilation failed");
        return false;
    }

    print_color(COLOR_BLUE, "Linking kernel...");
    char ld_cmd[65536];
    snprintf(ld_cmd, sizeof(ld_cmd), "ld -m elf_x86_64 -nostdlib -static -z max-page-size=0x1000 --gc-sections -T kernel/linker-scripts/x86_64.lds -o bin/kernel");
    
    for (int i = 0; i < objects.count; i++) {
        strcat(ld_cmd, " ");
        strcat(ld_cmd, objects.paths[i]);
    }
    
    if (system(ld_cmd) != 0) {
        print_color(COLOR_RED, "Linking failed");
        return false;
    }

    print_color(COLOR_GREEN, "Kernel linked: bin/kernel");
    return true;
}

bool create_iso() {
    print_color(COLOR_GREEN, "Creating ISO...");
    if (!build_limine()) return false;
    if (!file_exists("bin/kernel")) {
        print_color(COLOR_RED, "Kernel not found");
        return false;
    }

    rm_rf("iso_root");
    ensure_dir("iso_root/boot/limine");
    ensure_dir("iso_root/EFI/BOOT");
    ensure_dir("demo_iso");

    FILE *f = fopen("limine.conf", "w");
    if (f) {
        fprintf(f, "timeout: 3\n/%s %s\n    protocol: limine\n    path: boot():/boot/kernel\n", IMAGE_NAME, VERSION);
        fclose(f);
    }

    cmd_run(false, "cp bin/kernel iso_root/boot/");
    cmd_run(false, "cp limine.conf iso_root/boot/limine/");
    cmd_run(false, "cp limine/limine-bios.sys iso_root/boot/limine/");
    cmd_run(false, "cp limine/limine-bios-cd.bin iso_root/boot/limine/");
    cmd_run(false, "cp limine/limine-uefi-cd.bin iso_root/boot/limine/");
    cmd_run(false, "cp limine/BOOTX64.EFI iso_root/EFI/BOOT/");
    cmd_run(false, "cp limine/BOOTIA32.EFI iso_root/EFI/BOOT/");

    char timestamp[64];
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", tm);
    
    char iso_name[256];
    snprintf(iso_name, sizeof(iso_name), "demo_iso/%s.%s.%s.iso", IMAGE_NAME, VERSION, timestamp);

    char xorriso_cmd[4096];
    snprintf(xorriso_cmd, sizeof(xorriso_cmd), 
        "xorriso -as mkisofs -R -r -J -b boot/limine/limine-bios-cd.bin "
        "-no-emul-boot -boot-load-size 4 -boot-info-table -hfsplus "
        "-apm-block-size 2048 --efi-boot boot/limine/limine-uefi-cd.bin "
        "-efi-boot-part --efi-boot-image --protective-msdos-label "
        "iso_root -o %s", iso_name);
    
    if (cmd_run(true, xorriso_cmd) != 0) return false;

    cmd_run(true, "./limine/limine bios-install %s", iso_name);
    
    rm_rf("iso_root");
    
    char link_name[256];
    snprintf(link_name, sizeof(link_name), "demo_iso/%s.latest.iso", IMAGE_NAME);
    unlink(link_name);
    symlink(strrchr(iso_name, '/') + 1, link_name);

    print_color(COLOR_GREEN, "ISO Created: %s", iso_name);
    return true;
}

void generate_tree(const char *base_dir, FILE *out, int level) {
    DIR *dir;
    struct dirent *entry;

    if (!(dir = opendir(base_dir))) return;

    struct dirent **namelist;
    int n = scandir(base_dir, &namelist, NULL, alphasort);
    if (n < 0) return;

    for (int i = 0; i < n; i++) {
        entry = namelist[i];
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 || 
            strcmp(entry->d_name, ".git") == 0 || strcmp(entry->d_name, "obj") == 0 ||
            strcmp(entry->d_name, "bin") == 0 || strcmp(entry->d_name, "iso_root") == 0 ||
            strcmp(entry->d_name, "limine") == 0 || strcmp(entry->d_name, "limine-tools") == 0) {
            free(namelist[i]);
            continue;
        }

        for (int j = 0; j < level; j++) fprintf(out, "│   ");
        fprintf(out, "├── %s", entry->d_name);

        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", base_dir, entry->d_name);
        
        struct stat st;
        stat(path, &st);

        if (S_ISDIR(st.st_mode)) {
            fprintf(out, "/\n");
            generate_tree(path, out, level + 1);
        } else {
            fprintf(out, " (%ld bytes)\n", st.st_size);
            
            if (!ARG_STRUCTURE_ONLY) {
                const char *ext = strrchr(entry->d_name, '.');
                if (ext && (strcmp(ext, ".c") == 0 || strcmp(ext, ".h") == 0 || 
                            strcmp(ext, ".asm") == 0 || strcmp(ext, ".lds") == 0 ||
                            strcmp(ext, ".conf") == 0 || strcmp(ext, ".md") == 0)) {
                    
                    if (should_print_content(entry->d_name)) {
                        fprintf(out, "\n");
                        for (int j = 0; j < level + 1; j++) fprintf(out, "│   ");
                        fprintf(out, "│   Contents:\n");
                        
                        FILE *src = fopen(path, "r");
                        if (src) {
                            char line[4096];
                            int line_num = 1;
                            while (fgets(line, sizeof(line), src) && line_num <= 2000) {
                                line[strcspn(line, "\n")] = 0;
                                for (int j = 0; j < level + 1; j++) fprintf(out, "│   ");
                                fprintf(out, "│   %4d: %s\n", line_num++, line);
                            }
                            fclose(src);
                        }
                        fprintf(out, "\n");
                    }
                }
            }
        }
        free(namelist[i]);
    }
    free(namelist);
    closedir(dir);
}

void do_generate_tree() {
    FILE *f = fopen("OS-TREE.txt", "w");
    if (!f) return;
    fprintf(f, "OS Tree Structure - %s %s\n", IMAGE_NAME, VERSION);
    time_t t = time(NULL);
    fprintf(f, "Generated: %s\n", ctime(&t));
    if (TREE_FILES_COUNT > 0) {
        fprintf(f, "Showing contents for: ");
        for (int i = 0; i < TREE_FILES_COUNT; i++) fprintf(f, "%s ", TREE_FILES[i]);
        fprintf(f, "\n");
    }
    fprintf(f, "================================================================================\n\n");
    fprintf(f, "Directory Structure:\n.\n");
    generate_tree(".", f, 0);
    fclose(f);
    print_color(COLOR_GREEN, "Tree generated to OS-TREE.txt");
}

int main(int argc, char **argv) {
    char *command = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--tree") == 0) {
            ARG_TREE = true;
            int j = i + 1;
            while (j < argc && argv[j][0] != '-') {
                add_tree_file(argv[j]);
                j++;
            }
            i = j - 1;
        }
        else if (strcmp(argv[i], "--structure-only") == 0) ARG_STRUCTURE_ONLY = true;
        else if (strcmp(argv[i], "--no-clean") == 0) ARG_NO_CLEAN = true;
        else if (argv[i][0] != '-') command = argv[i];
    }

    if (ARG_TREE && !command) {
        do_generate_tree();
        return 0;
    }

    if (!command || strcmp(command, "help") == 0) {
        printf("Usage: ./builder/build [command] [options]\n");
        printf("Commands: run, clean, cleaniso, gitclean\n");
        printf("Options:\n");
        printf("  --tree [file1 file2 ...]  Generate OS-TREE.txt. If files listed, only show their content.\n");
        printf("  --no-clean                Don't clean build artifacts after run.\n");
        return 0;
    }

    if (strcmp(command, "clean") == 0) {
        for (int i = 0; DIRS_TO_CLEAN[i]; i++) rm_rf(DIRS_TO_CLEAN[i]);
        for (int i = 0; FILES_TO_CLEAN[i]; i++) if(file_exists(FILES_TO_CLEAN[i])) remove(FILES_TO_CLEAN[i]);
        cmd_run(false, "rm temp_* 2>/dev/null");
        print_color(COLOR_GREEN, "Cleanup complete");
        return 0;
    }

    if (strcmp(command, "cleaniso") == 0) {
        rm_rf("demo_iso");
        ensure_dir("demo_iso");
        return 0;
    }
    
    if (strcmp(command, "gitclean") == 0) {
        for (int i = 0; DIRS_TO_CLEAN[i]; i++) rm_rf(DIRS_TO_CLEAN[i]);
        for (int i = 0; FILES_TO_CLEAN[i]; i++) if(file_exists(FILES_TO_CLEAN[i])) remove(FILES_TO_CLEAN[i]);
        return 0;
    }

    if (strcmp(command, "run") == 0) {
        if (!compile_kernel() || !create_iso()) return 1;
        
        if (ARG_TREE) do_generate_tree();

        char iso_path[256];
        snprintf(iso_path, sizeof(iso_path), "demo_iso/%s.latest.iso", IMAGE_NAME);
        
        print_color(COLOR_GREEN, "Starting QEMU...");
        char qemu_cmd[1024];
        snprintf(qemu_cmd, sizeof(qemu_cmd), 
            "qemu-system-x86_64 -M q35 -cdrom %s -boot d -serial stdio %s", 
            iso_path, QEMUFLAGS);
        
        system(qemu_cmd);

        if (!ARG_NO_CLEAN) {
            rm_rf("obj");
            rm_rf("bin");
        }
        return 0;
    }

    print_color(COLOR_RED, "Unknown command: %s", command);
    return 1;
}