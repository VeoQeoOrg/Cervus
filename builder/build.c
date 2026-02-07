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
#include <fcntl.h>
#include <sys/wait.h>

#define IMAGE_NAME "Cervus"
#define VERSION "v0.0.1"
#define QEMUFLAGS "-m 12G -smp 8 -cpu qemu64,+fsgsbase"

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
    "sse.c", "fpu.c", "printf.c", "fabs.c", "pow.c", "pow10.c", "serial.c", "pmm.c", "paging.c", "apic.c", NULL
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
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "limine-tools/%s", DEPENDENCIES[i].name);

        if (!file_exists(path)) {
            print_color(COLOR_YELLOW, "Missing %s, setting up...", DEPENDENCIES[i].name);
            if (cmd_run(true, "git clone %s %s", DEPENDENCIES[i].url, path) != 0) return false;

            char git_cmd[PATH_MAX + 128];
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

    if (cmd_run(true, "git clone https://codeberg.org/Limine/Limine.git limine --branch=v10.6.3-binary --depth=1") != 0) return false;
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

    const char *linker_script = R"(
OUTPUT_FORMAT(elf64-x86-64)

ENTRY(kernel_main)

PHDRS
{
    limine_requests PT_LOAD;
    text PT_LOAD;
    rodata PT_LOAD;
    data PT_LOAD;
}

SECTIONS
{
    . = 0xffffffff80000000;

    .limine_requests : {
        KEEP(*(.limine_requests_start))
        KEEP(*(.limine_requests))
        KEEP(*(.limine_requests_end))
    } :limine_requests

    . = ALIGN(CONSTANT(MAXPAGESIZE));

    .text : {
        __start_isr_handlers = .;
        KEEP(*(.isr_handlers))
        __stop_isr_handlers = .;

        __start_irq_handlers = .;
        KEEP(*(.irq_handlers))
        __stop_irq_handlers = .;

        *(.text .text.*)
    } :text

    . = ALIGN(CONSTANT(MAXPAGESIZE));

    .rodata : {
        *(.rodata .rodata.*)
    } :rodata

    .note.gnu.build-id : {
        *(.note.gnu.build-id)
    } :rodata

    . = ALIGN(CONSTANT(MAXPAGESIZE));

    .data : {
        *(.data .data.*)
    } :data

    .bss : {
        *(.bss .bss.*)
        *(COMMON)
    } :data

    /DISCARD/ : {
        *(.eh_frame*)
        *(.note .note.*)
    }
}
)";

    FILE *f = fopen(lds_path, "w");
    if (!f) return;

    fprintf(f, "%s", linker_script);

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
            char path[PATH_MAX];
            snprintf(path, sizeof(path), "%s/%s", root_dir, entry->d_name);
            find_src_files(path, list);
        } else {
            const char *ext = strrchr(entry->d_name, '.');
            if (ext && (strcmp(ext, ".c") == 0 || strcmp(ext, ".asm") == 0 ||
                        strcmp(ext, ".S") == 0 || strcmp(ext, ".psf") == 0)) {
                char path[PATH_MAX];
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

        char obj_path[PATH_MAX];
        char flat_name[PATH_MAX];
        strcpy(flat_name, src);
        for(int j=0; flat_name[j]; j++) if(flat_name[j] == '/' || flat_name[j] == '.') flat_name[j] = '_';

        snprintf(obj_path, sizeof(obj_path), "obj/%s/%s.o", category, flat_name);
        file_list_add(&objects, obj_path);

        if (file_exists(obj_path) && get_mtime(src) <= get_mtime(obj_path)) continue;

        const char *ext = strrchr(src, '.');
        if (strcmp(ext, ".psf") == 0) {
            print_color(COLOR_BLUE, "Converting binary: %s", src);
            char temp_path[PATH_MAX];
            snprintf(temp_path, sizeof(temp_path), "temp_%s", strrchr(src, '/') + 1);
            cmd_run(false, "cp %s %s", src, temp_path);
            cmd_run(false, "objcopy -I binary -O elf64-x86-64 -B i386:x86-64 --rename-section .data=.rodata,alloc,load,readonly,data,contents %s %s", temp_path, obj_path);
            remove(temp_path);
            char stem[256]; strcpy(stem, strrchr(src, '/') + 1); *strrchr(stem, '.') = '\0';
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
            char final_flags[1024];
            if (sse) snprintf(final_flags, sizeof(final_flags), "%s %s", base_cflags, sse_cflags_suffix);
            else snprintf(final_flags, sizeof(final_flags), "%s %s", base_cflags, core_cflags);
            print_color(sse ? COLOR_MAGENTA : COLOR_CYAN, "[%s] %s", category, src);
            if (cmd_run(false, "gcc %s %s -c %s -o %s", final_flags, cppflags, src, obj_path) != 0) compilation_failed = true;
        }
    }

    if (compilation_failed) return false;

    print_color(COLOR_BLUE, "Linking kernel...");
    char ld_cmd[65536];
    snprintf(ld_cmd, sizeof(ld_cmd), "ld -m elf_x86_64 -nostdlib -static -z max-page-size=0x1000 --gc-sections -T kernel/linker-scripts/x86_64.lds -o bin/kernel");
    for (int i = 0; i < objects.count; i++) { strcat(ld_cmd, " "); strcat(ld_cmd, objects.paths[i]); }
    if (system(ld_cmd) != 0) return false;

    print_color(COLOR_GREEN, "Kernel linked: bin/kernel");
    return true;
}

bool create_iso() {
    print_color(COLOR_GREEN, "Creating ISO...");
    if (!build_limine()) return false;
    if (!file_exists("bin/kernel")) return false;

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
    cmd_run(false, "cp limine/limine-bios.sys limine/limine-bios-cd.bin limine/limine-uefi-cd.bin iso_root/boot/limine/");
    cmd_run(false, "cp limine/BOOTX64.EFI limine/BOOTIA32.EFI iso_root/EFI/BOOT/");

    char timestamp[64]; time_t t = time(NULL); strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", localtime(&t));
    char iso_name[PATH_MAX]; snprintf(iso_name, sizeof(iso_name), "demo_iso/%s.%s.%s.iso", IMAGE_NAME, VERSION, timestamp);

    char xorriso_cmd[PATH_MAX + 1024];
    snprintf(xorriso_cmd, sizeof(xorriso_cmd),
        "xorriso -as mkisofs -R -r -J -b boot/limine/limine-bios-cd.bin "
        "-no-emul-boot -boot-load-size 4 -boot-info-table -hfsplus "
        "-apm-block-size 2048 --efi-boot boot/limine/limine-uefi-cd.bin "
        "-efi-boot-part --efi-boot-image --protective-msdos-label "
        "iso_root -o %s", iso_name);

    if (cmd_run(true, xorriso_cmd) != 0) return false;
    cmd_run(true, "./limine/limine bios-install %s", iso_name);

    char link_name[PATH_MAX]; snprintf(link_name, sizeof(link_name), "demo_iso/%s.latest.iso", IMAGE_NAME);
    unlink(link_name); symlink(strrchr(iso_name, '/') + 1, link_name);
    rm_rf("iso_root");
    return true;
}

void check_sudo() {
    if (geteuid() != 0) {
        print_color(COLOR_RED, "This command requires root privileges. Please run with sudo.");
        exit(1);
    }
}

void list_iso_files(FileList *list) {
    DIR *dir = opendir("demo_iso");
    if (!dir) return;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strstr(entry->d_name, ".iso")) {
            char path[PATH_MAX];
            snprintf(path, sizeof(path), "demo_iso/%s", entry->d_name);
            file_list_add(list, path);
        }
    }
    closedir(dir);
}

void list_usb_devices(FileList *list) {
    DIR *dir = opendir("/sys/block");
    if (!dir) return;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "/sys/block/%s/removable", entry->d_name);
        FILE *f = fopen(path, "r");
        if (f) {
            int removable = 0;
            if (fscanf(f, "%d", &removable) == 1 && removable) {
                char dev_path[PATH_MAX];
                snprintf(dev_path, sizeof(dev_path), "/dev/%s", entry->d_name);
                file_list_add(list, dev_path);
            }
            fclose(f);
        }
    }
    closedir(dir);
}

void flash_iso() {
    check_sudo();
    FileList isos = {0}, devs = {0};
    list_iso_files(&isos);
    if (isos.count == 0) { print_color(COLOR_RED, "No ISO images found in demo_iso/"); return; }

    printf("\n%s--- SELECT ISO IMAGE ---%s\n", COLOR_CYAN, COLOR_RESET);
    for (int i = 0; i < isos.count; i++) printf("[%d] %s\n", i + 1, isos.paths[i]);
    int iso_choice; printf("Choice: "); if (scanf("%d", &iso_choice) < 1 || iso_choice > isos.count) return;

    list_usb_devices(&devs);
    if (devs.count == 0) { print_color(COLOR_RED, "No removable USB devices found!"); return; }

    printf("\n%s--- SELECT TARGET DEVICE ---%s\n", COLOR_RED, COLOR_RESET);
    for (int i = 0; i < devs.count; i++) {
        char model_path[PATH_MAX], model[256] = "Unknown Device";
        snprintf(model_path, sizeof(model_path), "/sys/block/%s/device/model", devs.paths[i] + 5);
        FILE *mf = fopen(model_path, "r");
        if (mf) { if(fgets(model, sizeof(model), mf)) model[strcspn(model, "\n")] = 0; fclose(mf); }
        printf("[%d] %s (%s)\n", i + 1, devs.paths[i], model);
    }
    int dev_choice; printf("Choice: "); if (scanf("%d", &dev_choice) < 1 || dev_choice > devs.count) return;

    printf("\n%sWARNING: ALL DATA ON %s WILL BE ERASED!%s\n", COLOR_BOLD, devs.paths[dev_choice-1], COLOR_RESET);
    printf("Type 'YES' to confirm: ");
    char confirm[10]; scanf("%s", confirm);
    if (strcmp(confirm, "YES") != 0) { printf("Aborted.\n"); return; }

    print_color(COLOR_YELLOW, "Flashing... This might take a while.");
    cmd_run(true, "dd if=%s of=%s bs=4M status=progress oflag=sync", isos.paths[iso_choice-1], devs.paths[dev_choice-1]);
    print_color(COLOR_GREEN, "Done!");
}

bool is_unreadable_file(const char *filename) {
    const char *no_ext_files[] = {"TODO", "LICENSE", "build", NULL};
    for (int i = 0; no_ext_files[i] != NULL; i++) {
        if (strcmp(filename, no_ext_files[i]) == 0) {
            return true;
        }
    }

    const char *ext = strrchr(filename, '.');
    if (!ext) return false;

    const char *binary_exts[] = {
        ".psf", ".jpg", ".png", ".jpeg", ".iso",
        ".hdd", ".img", ".bin", ".elf", ".o",
        ".txt", ".md", ".gitignore", ".json", NULL
    };

    for (int i = 0; binary_exts[i] != NULL; i++) {
        if (strcasecmp(ext, binary_exts[i]) == 0) {
            return true;
        }
    }

    return false;
}

void generate_tree_recursive(const char *base_dir, FILE *out, int level) {
    struct dirent **namelist;
    int n = scandir(base_dir, &namelist, NULL, alphasort);
    if (n < 0) return;

    const char *skip_dirs[] = {"wallpapers", "demo_iso", ".vscode", "builder", NULL};

    for (int i = 0; i < n; i++) {
        const char *name = namelist[i]->d_name;

        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0 ||
            strcmp(name, ".git") == 0 || strcmp(name, "obj") == 0 ||
            strcmp(name, "bin") == 0 || strcmp(name, "limine") == 0) {
            free(namelist[i]);
            continue;
        }

        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s", base_dir, name);
        struct stat st;
        stat(path, &st);

        if (S_ISDIR(st.st_mode)) {
            bool skip = false;
            for (int j = 0; skip_dirs[j] != NULL; j++) {
                if (strcmp(name, skip_dirs[j]) == 0) {
                    skip = true;
                    break;
                }
            }
            if (skip) {
                free(namelist[i]);
                continue;
            }
        }

        if (!S_ISDIR(st.st_mode) && is_unreadable_file(name)) {
            free(namelist[i]);
            continue;
        }

        for (int j = 0; j < level; j++) fprintf(out, "│   ");

        if (S_ISDIR(st.st_mode)) {
            fprintf(out, "├── %s/\n", name);
            generate_tree_recursive(path, out, level + 1);
        } else {
            fprintf(out, "├── %s (%ld bytes)\n", name, st.st_size);
            if (!ARG_STRUCTURE_ONLY && should_print_content(name)) {
                FILE *src = fopen(path, "r");
                if (src) {
                    char line[4096]; int ln = 1;
                    while (fgets(line, sizeof(line), src) && ln <= 500) {
                        for (int j = 0; j <= level; j++) fprintf(out, "│   ");
                        fprintf(out, "│ %4d: %s", ln++, line);
                    }
                    fclose(src);
                }
            }
        }
        free(namelist[i]);
    }
    free(namelist);
}

void do_generate_tree() {
    FILE *f = fopen("OS-TREE.txt", "w");
    if (!f) return;
    time_t t = time(NULL);
    fprintf(f, "OS Tree - %s %s | %s", IMAGE_NAME, VERSION, ctime(&t));
    generate_tree_recursive(".", f, 0);
    fclose(f);
    print_color(COLOR_GREEN, "Tree generated to OS-TREE.txt");
}

int main(int argc, char **argv) {
    char *command = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--tree") == 0) {
            ARG_TREE = true;
            int j = i + 1;
            while (j < argc && argv[j][0] != '-') add_tree_file(argv[j++]);
            i = j - 1;
        }
        else if (strcmp(argv[i], "--structure-only") == 0) ARG_STRUCTURE_ONLY = true;
        else if (strcmp(argv[i], "--no-clean") == 0) ARG_NO_CLEAN = true;
        else if (argv[i][0] != '-') command = argv[i];
    }

    if (ARG_TREE && !command) { do_generate_tree(); return 0; }
    if (!command || strcmp(command, "help") == 0) {
        printf("Usage: ./build [command] [options]\n");
        printf("Commands: run, flash, clean, cleaniso, gitclean\n");
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
        print_color(COLOR_GREEN, "Git-ready cleanup complete");
        return 0;
    }

    if (strcmp(command, "flash") == 0) {
        flash_iso();
        return 0;
    }

    if (strcmp(command, "run") == 0) {
        if (!compile_kernel() || !create_iso()) return 1;
        if (ARG_TREE) do_generate_tree();
        char iso_path[PATH_MAX]; snprintf(iso_path, sizeof(iso_path), "demo_iso/%s.latest.iso", IMAGE_NAME);
        print_color(COLOR_GREEN, "Starting QEMU...");
        cmd_run(false, "qemu-system-x86_64 -M q35 -cdrom %s -boot d -serial stdio %s", iso_path, QEMUFLAGS);
        if (!ARG_NO_CLEAN) { rm_rf("obj"); rm_rf("bin"); }
        return 0;
    }

    print_color(COLOR_RED, "Unknown command: %s", command);
    return 1;
}