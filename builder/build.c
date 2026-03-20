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

#define IMAGE_NAME   "Cervus"
#define VERSION      "v0.0.1"
#define QEMUFLAGS    "-m 8G -smp 8 -cpu qemu64,+fsgsbase -display gtk,grab-on-hover=on "

#define WALLPAPER_SRC "wallpapers/cervus1280x720.png"
#define WALLPAPER_DST "boot():/boot/wallpapers/cervus.png"

#define APPS_DIR   "apps"
#define SHELL_SRC  "apps/shell.c"
#define SHELL_ELF  "apps/shell.elf"

#define INITRAMFS_TAR      "initramfs.tar"
#define INITRAMFS_ROOTFS   "rootfs"

#define COLOR_RESET   "\033[0m"
#define COLOR_RED     "\033[91m"
#define COLOR_GREEN   "\033[92m"
#define COLOR_YELLOW  "\033[93m"
#define COLOR_BLUE    "\033[94m"
#define COLOR_MAGENTA "\033[95m"
#define COLOR_CYAN    "\033[96m"
#define COLOR_BOLD    "\033[1m"

const char *DIRS_TO_CLEAN[] = {
    "bin", "obj", "iso_root", "limine", "kernel/linker-scripts",
    "demo_iso", "limine-tools", "edk2-ovmf",
    INITRAMFS_ROOTFS,
    NULL
};
const char *FILES_TO_CLEAN[] = {
    "Cervus.iso", "Cervus.hdd",
    "kernel/.deps-obtained",
    "limine.conf",
    "OS-TREE.txt", "log.txt",
    "apps/shell.elf",
    INITRAMFS_TAR,
    NULL
};

const char *SSE_FILES[] = {
    "sse.c", "fpu.c", "printf.c", "fabs.c", "pow.c", "pow10.c",
    "serial.c", "pmm.c", "paging.c", "apic.c", "kernel.c", NULL
};

struct Dependency {
    const char *name;
    const char *url;
    const char *commit;
};

struct Dependency DEPENDENCIES[] = {
    {"freestnd-c-hdrs",  "https://codeberg.org/OSDev/freestnd-c-hdrs-0bsd.git",  "5df91dd7062ad0c54f5ffd86193bb9f008677631"},
    {"cc-runtime",       "https://codeberg.org/OSDev/cc-runtime.git",             "dae79833b57a01b9fd3e359ee31def69f5ae899b"},
    {"limine-protocol",  "https://codeberg.org/Limine/limine-protocol.git",       "c4616df2572d77c60020bdefa617dd9bdcc6566a"},
    {NULL, NULL, NULL}
};

bool ARG_NO_CLEAN       = false;
bool ARG_TREE           = false;
bool ARG_STRUCTURE_ONLY = false;
bool ARG_NO_INITRAMFS   = false;

char **TREE_FILES       = NULL;
int    TREE_FILES_COUNT = 0;
int    TREE_FILES_CAP   = 0;

void print_color(const char *color, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    printf("%s", color);
    vprintf(fmt, args);
    printf("%s\n", COLOR_RESET);
    va_end(args);
}

void add_tree_file(const char *filename) {
    if (TREE_FILES_COUNT >= TREE_FILES_CAP) {
        TREE_FILES_CAP = (TREE_FILES_CAP == 0) ? 8 : TREE_FILES_CAP * 2;
        TREE_FILES = realloc(TREE_FILES, TREE_FILES_CAP * sizeof(char *));
    }
    TREE_FILES[TREE_FILES_COUNT++] = strdup(filename);
}

bool should_print_content(const char *filename) {
    if (TREE_FILES_COUNT == 0) return true;
    for (int i = 0; i < TREE_FILES_COUNT; i++)
        if (strcmp(filename, TREE_FILES[i]) == 0) return true;
    return false;
}

int cmd_run(bool verbose, const char *fmt, ...) {
    char cmd[8192];
    va_list args;
    va_start(args, fmt);
    vsnprintf(cmd, sizeof(cmd), fmt, args);
    va_end(args);
    if (verbose) print_color(COLOR_BLUE, "Running: %s", cmd);
    int ret = system(cmd);
    return (ret != 0) ? WEXITSTATUS(ret) : 0;
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

bool setup_dependencies(void) {
    print_color(COLOR_GREEN, "Checking dependencies...");
    ensure_dir("limine-tools");

    for (int i = 0; DEPENDENCIES[i].name != NULL; i++) {
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "limine-tools/%s", DEPENDENCIES[i].name);

        if (!file_exists(path)) {
            print_color(COLOR_YELLOW, "Missing %s, setting up...", DEPENDENCIES[i].name);
            if (cmd_run(true, "git clone %s %s", DEPENDENCIES[i].url, path) != 0) return false;
            char gc[PATH_MAX + 128];
            snprintf(gc, sizeof(gc),
                     "git -C %s -c advice.detachedHead=false checkout %s",
                     path, DEPENDENCIES[i].commit);
            if (system(gc) != 0) return false;
        }
    }
    return true;
}

bool build_limine(void) {
    if (file_exists("limine/limine")) {
        print_color(COLOR_GREEN, "Limine already built");
        return true;
    }
    print_color(COLOR_GREEN, "Building Limine...");
    if (file_exists("limine")) rm_rf("limine");
    if (cmd_run(true, "git clone https://codeberg.org/Limine/Limine.git limine "
                      "--branch=v10.8.5-binary --depth=1") != 0) return false;
    if (cmd_run(true, "make -C limine") != 0) return false;
    return true;
}

void ensure_linker_script(void) {
    ensure_dir("kernel/linker-scripts");
    const char *lds_path = "kernel/linker-scripts/x86_64.lds";
    if (file_exists(lds_path)) return;

    const char *script =
"OUTPUT_FORMAT(elf64-x86-64)\n"
"ENTRY(kernel_main)\n"
"PHDRS {\n"
"    limine_requests PT_LOAD;\n"
"    text PT_LOAD;\n"
"    rodata PT_LOAD;\n"
"    data PT_LOAD;\n"
"}\n"
"SECTIONS {\n"
"    . = 0xffffffff80000000;\n"
"    .limine_requests : {\n"
"        KEEP(*(.limine_requests_start))\n"
"        KEEP(*(.limine_requests))\n"
"        KEEP(*(.limine_requests_end))\n"
"    } :limine_requests\n"
"    . = ALIGN(CONSTANT(MAXPAGESIZE));\n"
"    .text : {\n"
"        __start_isr_handlers = .;\n"
"        KEEP(*(.isr_handlers))\n"
"        __stop_isr_handlers = .;\n"
"        __start_irq_handlers = .;\n"
"        KEEP(*(.irq_handlers))\n"
"        __stop_irq_handlers = .;\n"
"        *(.text .text.*)\n"
"    } :text\n"
"    . = ALIGN(CONSTANT(MAXPAGESIZE));\n"
"    .rodata : { *(.rodata .rodata.*) } :rodata\n"
"    .note.gnu.build-id : { *(.note.gnu.build-id) } :rodata\n"
"    . = ALIGN(CONSTANT(MAXPAGESIZE));\n"
"    .data : {\n"
"        *(.data .data.*)\n"
"        . = ALIGN(4096);\n"
"        __percpu_start = .;\n"
"        KEEP(*(.percpu .percpu.*))\n"
"        . = ALIGN(4096);\n"
"        __percpu_end = .;\n"
"    } :data\n"
"    .bss : { *(.bss .bss.*) *(COMMON) } :data\n"
"    /DISCARD/ : { *(.eh_frame*) *(.note .note.*) }\n"
"}\n";

    FILE *f = fopen(lds_path, "w");
    if (!f) return;
    fprintf(f, "%s", script);
    fclose(f);
    print_color(COLOR_GREEN, "x86_64.lds created");
}

typedef struct {
    char src[512];
    char elf[512];
    char name[256];
} app_entry_t;

#define MAX_APPS 64
static app_entry_t g_apps[MAX_APPS];
static int         g_naps = 0;

static int scan_apps(void) {
    g_naps = 0;
    DIR *d = opendir(APPS_DIR);
    if (!d) {
        print_color(COLOR_RED, "[apps] Cannot open directory '%s'", APPS_DIR);
        return 0;
    }
    struct dirent *de;
    while ((de = readdir(d)) != NULL && g_naps < MAX_APPS) {
        const char *nm = de->d_name;
        size_t nlen = strlen(nm);
        if (nlen < 3) continue;
        if (nm[0] == '.') continue;
        if (strcmp(nm + nlen - 2, ".c") != 0) continue;

        app_entry_t *e = &g_apps[g_naps];
        snprintf(e->src,  sizeof(e->src),  "%s/%s",   APPS_DIR, nm);
        snprintf(e->elf,  sizeof(e->elf),  "%s/%.*s.elf", APPS_DIR, (int)(nlen-2), nm);
        snprintf(e->name, sizeof(e->name), "%.*s", (int)(nlen-2), nm);
        g_naps++;
    }
    closedir(d);
    for (int i = 1; i < g_naps; i++) {
        app_entry_t tmp = g_apps[i]; int j = i-1;
        while (j >= 0 && strcmp(g_apps[j].name, tmp.name) > 0) {
            g_apps[j+1] = g_apps[j]; j--;
        }
        g_apps[j+1] = tmp;
    }
    return g_naps;
}

static bool build_one_app(const app_entry_t *e) {
    if (file_exists(e->elf) && get_mtime(e->src) <= get_mtime(e->elf)) {
        print_color(COLOR_GREEN, "[ELF] %s is up to date", e->elf);
        return true;
    }
    print_color(COLOR_CYAN, "[ELF] Compiling %s -> %s", e->src, e->elf);
    int ret = cmd_run(false,
        "gcc -ffreestanding -nostdlib -static -fno-stack-protector"
        " -O0 -g -I" APPS_DIR
        " -Wl,-Ttext-segment=0x401000 -Wl,-e,_start"
        " -o %s %s",
        e->elf, e->src);
    if (ret != 0) {
        print_color(COLOR_RED, "[ELF] Failed to compile %s", e->src);
        return false;
    }
    print_color(COLOR_GREEN, "[ELF] %s built successfully", e->elf);
    return true;
}

bool build_all_apps(void) {
    scan_apps();
    if (g_naps == 0) {
        print_color(COLOR_YELLOW, "[apps] No .c files found in '%s'", APPS_DIR);
        return true;
    }
    print_color(COLOR_CYAN, "[apps] Found %d app(s) in '%s'", g_naps, APPS_DIR);
    for (int i = 0; i < g_naps; i++) {
        if (!build_one_app(&g_apps[i])) return false;
    }
    return true;
}

void clean_apps_elfs(void) {
    DIR *d = opendir(APPS_DIR);
    if (!d) return;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        const char *nm = de->d_name;
        size_t nlen = strlen(nm);
        if (nlen < 5) continue;
        if (strcmp(nm + nlen - 4, ".elf") != 0) continue;
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", APPS_DIR, nm);
        remove(path);
        print_color(COLOR_YELLOW, "[clean] removed %s", path);
    }
    closedir(d);
}


bool build_initramfs(void) {
    if (ARG_NO_INITRAMFS) {
        print_color(COLOR_YELLOW, "[initramfs] skipped (--no-initramfs)");
        return true;
    }

    bool tar_exists = file_exists(INITRAMFS_TAR);
    bool any_newer = false;
    if (!tar_exists) {
        any_newer = true;
    } else {
        scan_apps();
        for (int i = 0; i < g_naps; i++) {
            if (file_exists(g_apps[i].elf) &&
                get_mtime(g_apps[i].elf) > get_mtime(INITRAMFS_TAR)) {
                any_newer = true; break;
            }
        }
    }
    if (tar_exists && !any_newer) {
        print_color(COLOR_GREEN, "[initramfs] %s is up to date", INITRAMFS_TAR);
        return true;
    }

    print_color(COLOR_CYAN, "[initramfs] Building rootfs...");

    const char *dirs[] = {
        INITRAMFS_ROOTFS "/bin",
        INITRAMFS_ROOTFS "/dev",
        INITRAMFS_ROOTFS "/etc",
        INITRAMFS_ROOTFS "/tmp",
        INITRAMFS_ROOTFS "/proc",
        NULL
    };
    for (int i = 0; dirs[i]; i++) ensure_dir(dirs[i]);

    FILE *passwd = fopen(INITRAMFS_ROOTFS "/etc/passwd", "w");
    if (passwd) {
        fprintf(passwd, "root:x:0:0:root:/root:/bin/sh\n");
        fclose(passwd);
    }

    FILE *hostname = fopen(INITRAMFS_ROOTFS "/etc/hostname", "w");
    if (hostname) {
        fprintf(hostname, "cervus\n");
        fclose(hostname);
    }

    FILE *motd = fopen(INITRAMFS_ROOTFS "/etc/motd", "w");
    if (motd) {
        fprintf(motd,
            "\n"
            "    $$$$$$\\                                                    \n"
            "   $$  __$$\\                                                   \n"
            "   $$ /  \\__| $$$$$$\\   $$$$$$\\ $$\\    $$\\ $$\\   $$\\  $$$$$$$\\ \n"
            "   $$ |      $$  __$$\\ $$  __$$\\\\$$\\  $$  |$$ |  $$ |$$  _____|\n"
            "   $$ |      $$$$$$$$ |$$ |  \\__|\\$$\\$$  / $$ |  $$ |\\$$$$$$\\  \n"
            "   $$ |  $$\\ $$   ____|$$ |       \\$$$  /  $$ |  $$ | \\____$$\\ \n"
            "   \\$$$$$$  |\\$$$$$$$\\ $$ |        \\$  /   \\$$$$$$  |$$$$$$$  |\n"
            "    \\______/  \\______||\\__|         \\_/     \\______/ \\_______/\n"
            "\n"
            " Cervus OS " VERSION " (Alpha release)\n"
            "\n"
            " Type 'help' to see available commands.\n"
            "\n");
        fclose(motd);
    }

    if (file_exists(SHELL_ELF)) {
        if (cmd_run(false, "cp %s %s/bin/init", SHELL_ELF, INITRAMFS_ROOTFS) != 0) {
            print_color(COLOR_RED, "[initramfs] Failed to copy shell.elf -> bin/init");
            return false;
        }
        cmd_run(false, "cp %s %s/bin/shell", SHELL_ELF, INITRAMFS_ROOTFS);
        print_color(COLOR_GREEN, "[initramfs] shell.elf -> /bin/init + /bin/shell");
    } else {
        print_color(COLOR_RED, "[initramfs] shell.elf not found — boot will drop to nothing!");
        FILE *stub = fopen(INITRAMFS_ROOTFS "/bin/.keep", "w");
        if (stub) fclose(stub);
    }

    ensure_dir(INITRAMFS_ROOTFS "/apps");
    scan_apps();
    for (int i = 0; i < g_naps; i++) {
        if (strcmp(g_apps[i].name, "shell") == 0) continue;
        if (!file_exists(g_apps[i].elf)) continue;
        char dst[512];
        snprintf(dst, sizeof(dst), "%s/apps/%s", INITRAMFS_ROOTFS, g_apps[i].name);
        if (cmd_run(false, "cp %s %s", g_apps[i].elf, dst) != 0) {
            print_color(COLOR_RED, "[initramfs] Failed to copy %s", g_apps[i].elf);
        } else {
            print_color(COLOR_GREEN, "[initramfs] %s -> rootfs/apps/%s",
                        g_apps[i].elf, g_apps[i].name);
        }
    }

    FILE *readme = fopen(INITRAMFS_ROOTFS "/etc/readme.txt", "w");
    if (readme) {
        fprintf(readme,
            "Cervus OS v0.0.1\n"
            "================\n"
            "\n"
            "This is Cervus — a hobby x86_64 OS written in C.\n"
            "With Limine Bootloader.\n"
            "\n"
            "Shell commands:\n"
            "  help, ls, cat, cd, pwd\n"
            "  meminfo, cpuinfo, uname, clear, echo\n"
            "\n"
            "Source: https://github.com/VeoQeo/Cervus\n"
        );
        fclose(readme);
        print_color(COLOR_GREEN, "[initramfs] created /etc/readme.txt");
    }

    FILE *welcome = fopen(INITRAMFS_ROOTFS "/etc/welcome.txt", "w");
    if (welcome) {
        fprintf(welcome,
            "Welcome to Cervus Shell!\n"
            "\n"
            "Tips:\n"
            "  - Use arrow keys to move cursor within a command\n"
            "  - Use Up/Down to browse command history (max 20)\n"
            "  - Type 'ls' to list files, 'cat <file>' to read .txt files\n"
            "  - Binaries are in /bin and /apps\n"
        );
        fclose(welcome);
        print_color(COLOR_GREEN, "[initramfs] created /etc/welcome.txt");
    }

    print_color(COLOR_CYAN, "[initramfs] Packing %s...", INITRAMFS_TAR);
    if (cmd_run(false,
            "tar --format=ustar -cf %s -C %s .",
            INITRAMFS_TAR, INITRAMFS_ROOTFS) != 0) {
        print_color(COLOR_RED, "[initramfs] tar failed");
        return false;
    }

    print_color(COLOR_GREEN, "[initramfs] Contents of %s:", INITRAMFS_TAR);
    cmd_run(false, "tar -tvf %s", INITRAMFS_TAR);

    print_color(COLOR_GREEN, "[initramfs] %s built successfully", INITRAMFS_TAR);
    return true;
}

typedef struct {
    char **paths;
    int    count;
    int    capacity;
} FileList;

void file_list_add(FileList *list, const char *path) {
    if (list->count >= list->capacity) {
        list->capacity = list->capacity == 0 ? 16 : list->capacity * 2;
        list->paths = realloc(list->paths, list->capacity * sizeof(char *));
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
            if (ext && (strcmp(ext, ".c")   == 0 || strcmp(ext, ".asm") == 0 ||
                        strcmp(ext, ".S")   == 0 || strcmp(ext, ".psf") == 0)) {
                char path[PATH_MAX];
                snprintf(path, sizeof(path), "%s/%s", root_dir, entry->d_name);
                file_list_add(list, path);
            }
        }
    }
    closedir(dir);
}

bool is_sse_file(const char *filename) {
    for (int i = 0; SSE_FILES[i] != NULL; i++)
        if (strstr(filename, SSE_FILES[i]) != NULL) return true;
    return false;
}

bool compile_kernel(void) {
    print_color(COLOR_GREEN, "Compiling kernel...");
    ensure_linker_script();
    if (!setup_dependencies()) return false;

    if (!build_all_apps()) return false;

    ensure_dir("bin");
    ensure_dir("obj/kernel");
    ensure_dir("obj/libc");

    const char *base_cflags =
        "-g -O2 -pipe -Wall -Wextra -std=gnu11 -nostdinc -ffreestanding "
        "-fno-stack-protector -fno-stack-check -fno-lto -fno-PIC "
        "-ffunction-sections -fdata-sections "
        "-m64 -march=x86-64 -mabi=sysv -mcmodel=kernel "
        "-mno-red-zone -mgeneral-regs-only";

    const char *core_cflags = "-mno-sse -mno-sse2 -mno-mmx -mno-3dnow";
    const char *sse_cflags  = "-msse -msse2 -mfpmath=sse -mno-mmx -mno-3dnow";

    char cppflags[2048];
    snprintf(cppflags, sizeof(cppflags),
        "-I kernel/src"
        " -I libc/include"
        " -I limine-tools/limine-protocol/include"
        " -isystem limine-tools/freestnd-c-hdrs/include"
        " -MMD -MP");

    FileList sources = {0};
    if (file_exists("kernel/src")) find_src_files("kernel/src", &sources);
    if (file_exists("libc/src"))   find_src_files("libc/src",   &sources);

    FileList objects = {0};
    bool failed = false;

    for (int i = 0; i < sources.count; i++) {
        char *src = sources.paths[i];

        char category[16] = "other";
        if (strncmp(src, "kernel/", 7) == 0) strcpy(category, "kernel");
        else if (strncmp(src, "libc/",   5) == 0) strcpy(category, "libc");

        char obj_path[PATH_MAX];
        char flat[PATH_MAX];
        strcpy(flat, src);
        for (int j = 0; flat[j]; j++)
            if (flat[j] == '/' || flat[j] == '.') flat[j] = '_';
        snprintf(obj_path, sizeof(obj_path), "obj/%s/%s.o", category, flat);
        file_list_add(&objects, obj_path);

        if (file_exists(obj_path) && get_mtime(src) <= get_mtime(obj_path)) continue;

        const char *ext = strrchr(src, '.');

        if (strcmp(ext, ".psf") == 0) {
            print_color(COLOR_BLUE, "Converting binary: %s", src);
            char tmp[PATH_MAX];
            snprintf(tmp, sizeof(tmp), "temp_%s", strrchr(src, '/') + 1);
            cmd_run(false, "cp %s %s", src, tmp);
            cmd_run(false, "objcopy -I binary -O elf64-x86-64 -B i386:x86-64 "
                           "--rename-section .data=.rodata,alloc,load,readonly,data,contents "
                           "%s %s", tmp, obj_path);
            remove(tmp);
            char stem[256];
            strcpy(stem, strrchr(src, '/') + 1);
            *strrchr(stem, '.') = '\0';
            char rsym[1024];
            snprintf(rsym, sizeof(rsym),
                "--redefine-sym _binary_temp_%s_psf_start=_binary_%s_psf_start "
                "--redefine-sym _binary_temp_%s_psf_end=_binary_%s_psf_end "
                "--redefine-sym _binary_temp_%s_psf_size=_binary_%s_psf_size",
                stem, stem, stem, stem, stem, stem);
            cmd_run(false, "objcopy %s %s", rsym, obj_path);

        } else if (strcmp(ext, ".asm") == 0) {
            print_color(COLOR_CYAN, "[asm] %s", src);
            if (cmd_run(false, "nasm -g -F dwarf -f elf64 %s -o %s", src, obj_path) != 0)
                failed = true;

        } else {
            bool sse = is_sse_file(src);
            char flags[1024];
            snprintf(flags, sizeof(flags), "%s %s", base_cflags,
                     sse ? sse_cflags : core_cflags);
            print_color(sse ? COLOR_MAGENTA : COLOR_CYAN, "[%s] %s", category, src);
            if (cmd_run(false, "gcc %s %s -c %s -o %s",
                        flags, cppflags, src, obj_path) != 0)
                failed = true;
        }
    }

    if (failed) return false;

    print_color(COLOR_BLUE, "Linking kernel...");
    char ld_cmd[65536];
    snprintf(ld_cmd, sizeof(ld_cmd),
        "ld -m elf_x86_64 -nostdlib -static -z max-page-size=0x1000 "
        "--gc-sections -T kernel/linker-scripts/x86_64.lds -o bin/kernel");
    for (int i = 0; i < objects.count; i++) {
        strcat(ld_cmd, " ");
        strcat(ld_cmd, objects.paths[i]);
    }
    if (system(ld_cmd) != 0) return false;

    print_color(COLOR_GREEN, "Kernel linked: bin/kernel");
    return true;
}

bool create_iso(void) {
    print_color(COLOR_GREEN, "Creating ISO...");
    if (!build_limine()) return false;
    if (!file_exists("bin/kernel")) {
        print_color(COLOR_RED, "bin/kernel not found!");
        return false;
    }

    rm_rf("iso_root");
    ensure_dir("iso_root/boot/limine");
    ensure_dir("iso_root/boot/wallpapers");
    ensure_dir("iso_root/EFI/BOOT");
    ensure_dir("demo_iso");

    if (file_exists(WALLPAPER_SRC)) {
        cmd_run(false, "cp %s iso_root/boot/wallpapers/cervus.png", WALLPAPER_SRC);
        print_color(COLOR_GREEN, "Wallpaper copied");
    } else {
        print_color(COLOR_YELLOW, "Warning: wallpaper not found at %s", WALLPAPER_SRC);
    }

    cmd_run(false, "cp bin/kernel iso_root/boot/kernel");

    bool has_elf = file_exists(SHELL_ELF);
    if (has_elf) {
        cmd_run(false, "cp %s iso_root/boot/shell.elf", SHELL_ELF);
        print_color(COLOR_GREEN, "[module 0] shell.elf -> iso_root/boot/shell.elf");
    } else {
        print_color(COLOR_RED, "[module 0] shell.elf not found — boot will fail!");
    }

    bool has_initramfs = !ARG_NO_INITRAMFS && file_exists(INITRAMFS_TAR);
    if (has_initramfs) {
        cmd_run(false, "cp %s iso_root/boot/initramfs.tar", INITRAMFS_TAR);
        print_color(COLOR_GREEN, "[module 1] initramfs.tar -> iso_root/boot/initramfs.tar");
    } else {
        print_color(COLOR_YELLOW, "[module 1] initramfs.tar not found — booting without rootfs");
    }

    FILE *f = fopen("limine.conf", "w");
    if (!f) { print_color(COLOR_RED, "Cannot create limine.conf"); return false; }

    if (file_exists(WALLPAPER_SRC))
        fprintf(f, "wallpaper: %s\n", WALLPAPER_DST);

    fprintf(f,
        "timeout: 3\n"
        "\n"
        "/%s %s\n"
        "    protocol: limine\n"
        "    path: boot():/boot/kernel\n",
        IMAGE_NAME, VERSION);

    if (has_elf) {
        fprintf(f,
            "    module_path: boot():/boot/shell.elf\n"
            "    module_cmdline: init\n");
        print_color(COLOR_GREEN, "[limine.conf] module 0: shell.elf (init)");
    }

    if (has_initramfs) {
        fprintf(f,
            "    module_path: boot():/boot/initramfs.tar\n"
            "    module_cmdline: initramfs\n");
        print_color(COLOR_GREEN, "[limine.conf] module 1: initramfs.tar");
    }

    fclose(f);

    print_color(COLOR_CYAN, "--- limine.conf ---");
    cmd_run(false, "cat limine.conf");
    print_color(COLOR_CYAN, "-------------------");

    cmd_run(false, "cp limine.conf iso_root/boot/limine/");
    cmd_run(false, "cp limine/limine-bios.sys limine/limine-bios-cd.bin "
                   "limine/limine-uefi-cd.bin iso_root/boot/limine/");
    cmd_run(false, "cp limine/BOOTX64.EFI limine/BOOTIA32.EFI iso_root/EFI/BOOT/");

    char timestamp[64];
    time_t t = time(NULL);
    strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", localtime(&t));

    char iso_name[PATH_MAX];
    snprintf(iso_name, sizeof(iso_name),
             "demo_iso/%s.%s.%s.iso", IMAGE_NAME, VERSION, timestamp);

    char xorriso[PATH_MAX + 1024];
    snprintf(xorriso, sizeof(xorriso),
        "xorriso -as mkisofs -R -r -J"
        " -b boot/limine/limine-bios-cd.bin"
        " -no-emul-boot -boot-load-size 4 -boot-info-table"
        " -hfsplus -apm-block-size 2048"
        " --efi-boot boot/limine/limine-uefi-cd.bin"
        " -efi-boot-part --efi-boot-image --protective-msdos-label"
        " iso_root -o %s", iso_name);

    if (cmd_run(true, xorriso) != 0) return false;
    cmd_run(true, "./limine/limine bios-install %s", iso_name);

    char link[PATH_MAX];
    snprintf(link, sizeof(link), "demo_iso/%s.latest.iso", IMAGE_NAME);
    unlink(link);
    symlink(strrchr(iso_name, '/') + 1, link);

    rm_rf("iso_root");

    print_color(COLOR_GREEN, "ISO ready: %s", iso_name);
    return true;
}

void check_sudo(void) {
    if (geteuid() != 0) {
        print_color(COLOR_RED, "This command requires root. Run with sudo.");
        exit(1);
    }
}

void list_iso_files(FileList *list) {
    DIR *dir = opendir("demo_iso");
    if (!dir) return;
    struct dirent *e;
    while ((e = readdir(dir)) != NULL) {
        if (strstr(e->d_name, ".iso")) {
            char path[PATH_MAX];
            snprintf(path, sizeof(path), "demo_iso/%s", e->d_name);
            file_list_add(list, path);
        }
    }
    closedir(dir);
}

void list_usb_devices(FileList *list) {
    DIR *dir = opendir("/sys/block");
    if (!dir) return;
    struct dirent *e;
    while ((e = readdir(dir)) != NULL) {
        if (e->d_name[0] == '.') continue;
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "/sys/block/%s/removable", e->d_name);
        FILE *f = fopen(path, "r");
        if (f) {
            int removable = 0;
            if (fscanf(f, "%d", &removable) == 1 && removable) {
                char dev[PATH_MAX];
                snprintf(dev, sizeof(dev), "/dev/%s", e->d_name);
                file_list_add(list, dev);
            }
            fclose(f);
        }
    }
    closedir(dir);
}

void flash_iso(void) {
    check_sudo();
    FileList isos = {0}, devs = {0};
    list_iso_files(&isos);
    if (isos.count == 0) { print_color(COLOR_RED, "No ISO found in demo_iso/"); return; }

    printf("\n%s--- SELECT ISO ---%s\n", COLOR_CYAN, COLOR_RESET);
    for (int i = 0; i < isos.count; i++) printf("[%d] %s\n", i + 1, isos.paths[i]);
    int ic; printf("Choice: ");
    if (scanf("%d", &ic) < 1 || ic < 1 || ic > isos.count) return;

    list_usb_devices(&devs);
    if (devs.count == 0) { print_color(COLOR_RED, "No removable USB devices found!"); return; }

    printf("\n%s--- SELECT DEVICE ---%s\n", COLOR_RED, COLOR_RESET);
    for (int i = 0; i < devs.count; i++) {
        char mpath[PATH_MAX], model[256] = "Unknown";
        snprintf(mpath, sizeof(mpath), "/sys/block/%s/device/model", devs.paths[i] + 5);
        FILE *mf = fopen(mpath, "r");
        if (mf) { if (fgets(model, sizeof(model), mf)) model[strcspn(model, "\n")] = 0; fclose(mf); }
        printf("[%d] %s (%s)\n", i + 1, devs.paths[i], model);
    }
    int dc; printf("Choice: ");
    if (scanf("%d", &dc) < 1 || dc < 1 || dc > devs.count) return;

    printf("\n%sWARNING: ALL DATA ON %s WILL BE ERASED!%s\n",
           COLOR_BOLD, devs.paths[dc - 1], COLOR_RESET);
    printf("Type 'YES' to confirm: ");
    char confirm[10]; scanf("%s", confirm);
    if (strcmp(confirm, "YES") != 0) { printf("Aborted.\n"); return; }

    print_color(COLOR_YELLOW, "Flashing %s -> %s ...", isos.paths[ic - 1], devs.paths[dc - 1]);
    cmd_run(true, "dd if=%s of=%s bs=4M status=progress oflag=sync",
            isos.paths[ic - 1], devs.paths[dc - 1]);
    print_color(COLOR_GREEN, "Done!");
}

bool is_unreadable_file(const char *filename) {
    const char *skip_names[] = { "TODO", "LICENSE", "build", NULL };
    for (int i = 0; skip_names[i]; i++)
        if (strcmp(filename, skip_names[i]) == 0) return true;

    const char *ext = strrchr(filename, '.');
    if (!ext) return false;

    const char *bin_exts[] = {
        ".psf", ".jpg", ".png", ".jpeg", ".iso", ".hdd", ".img",
        ".bin", ".elf", ".o", ".txt", ".md", ".gitignore", ".json",
        ".tar",
        NULL
    };
    for (int i = 0; bin_exts[i]; i++)
        if (strcasecmp(ext, bin_exts[i]) == 0) return true;
    return false;
}

void generate_tree_recursive(const char *base_dir, FILE *out, int level) {
    struct dirent **namelist;
    int n = scandir(base_dir, &namelist, NULL, alphasort);
    if (n < 0) return;

    const char *skip_dirs[] = {
        "wallpapers", "demo_iso", ".vscode", "builder",
        INITRAMFS_ROOTFS,
        NULL
    };

    for (int i = 0; i < n; i++) {
        const char *name = namelist[i]->d_name;

        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0 ||
            strcmp(name, ".git") == 0 || strcmp(name, "obj") == 0 ||
            strcmp(name, "bin") == 0 || strcmp(name, "limine") == 0) {
            free(namelist[i]); continue;
        }

        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s", base_dir, name);
        struct stat st;
        stat(path, &st);

        if (S_ISDIR(st.st_mode)) {
            bool skip = false;
            for (int j = 0; skip_dirs[j]; j++)
                if (strcmp(name, skip_dirs[j]) == 0) { skip = true; break; }
            if (skip) { free(namelist[i]); continue; }
        }

        if (!S_ISDIR(st.st_mode) && is_unreadable_file(name)) {
            free(namelist[i]); continue;
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
                    while (fgets(line, sizeof(line), src) && ln <= 5000) {
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

void do_generate_tree(void) {
    FILE *f = fopen("OS-TREE.txt", "w");
    if (!f) return;
    time_t t = time(NULL);
    fprintf(f, "OS Tree - %s %s | %s", IMAGE_NAME, VERSION, ctime(&t));
    generate_tree_recursive(".", f, 0);
    fclose(f);
    print_color(COLOR_GREEN, "Tree generated to OS-TREE.txt");
}

static void print_help(void) {
    printf("%sCervus OS build system%s\n\n", COLOR_BOLD, COLOR_RESET);
    printf("Usage: ./build [command] [options]\n\n");
    printf("Commands:\n");
    printf("  run            Build kernel + initramfs + ISO, then launch QEMU\n");
    printf("  flash          Flash latest ISO to USB device (requires sudo)\n");
    printf("  clean          Remove all build artifacts\n");
    printf("  cleaniso       Remove only ISO images in demo_iso/\n");
    printf("  gitclean       Same as clean (for git commit prep)\n");
    printf("  help           Show this message\n\n");
    printf("Options:\n");
    printf("  --tree [files] Generate OS-TREE.txt (optional: only list files)\n");
    printf("  --structure-only  Generate tree without file contents\n");
    printf("  --no-clean     Keep obj/ and bin/ after run\n");
    printf("  --no-initramfs Skip initramfs.tar creation\n\n");
    printf("Examples:\n");
    printf("  ./build run\n");
    printf("  ./build run --no-initramfs\n");
    printf("  ./build run --tree\n");
    printf("  ./build --tree kernel.c vfs.c\n");
}

int main(int argc, char **argv) {
    char *command = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--tree") == 0) {
            ARG_TREE = true;
            int j = i + 1;
            while (j < argc && argv[j][0] != '-') add_tree_file(argv[j++]);
            i = j - 1;
        } else if (strcmp(argv[i], "--structure-only") == 0) {
            ARG_STRUCTURE_ONLY = true;
        } else if (strcmp(argv[i], "--no-clean") == 0) {
            ARG_NO_CLEAN = true;
        } else if (strcmp(argv[i], "--no-initramfs") == 0) {
            ARG_NO_INITRAMFS = true;
        } else if (argv[i][0] != '-') {
            command = argv[i];
        }
    }

    if (ARG_TREE && !command) { do_generate_tree(); return 0; }

    if (!command || strcmp(command, "help") == 0) {
        print_help();
        return 0;
    }

    if (strcmp(command, "clean") == 0 || strcmp(command, "gitclean") == 0) {
        for (int i = 0; DIRS_TO_CLEAN[i];  i++) rm_rf(DIRS_TO_CLEAN[i]);
        clean_apps_elfs();
        for (int i = 0; FILES_TO_CLEAN[i]; i++)
            if (file_exists(FILES_TO_CLEAN[i])) remove(FILES_TO_CLEAN[i]);
        cmd_run(false, "rm -f temp_* 2>/dev/null");
        print_color(COLOR_GREEN, "Cleanup complete");
        return 0;
    }

    if (strcmp(command, "cleaniso") == 0) {
        rm_rf("demo_iso");
        ensure_dir("demo_iso");
        return 0;
    }

    if (strcmp(command, "flash") == 0) {
        flash_iso();
        return 0;
    }

    if (strcmp(command, "run") == 0) {
        if (!compile_kernel()) {
            print_color(COLOR_RED, "Kernel compilation failed");
            return 1;
        }

        if (!build_initramfs()) {
            print_color(COLOR_RED, "initramfs build failed");
            return 1;
        }

        if (!create_iso()) {
            print_color(COLOR_RED, "ISO creation failed");
            return 1;
        }

        if (ARG_TREE) do_generate_tree();

        char iso_path[PATH_MAX];
        snprintf(iso_path, sizeof(iso_path), "demo_iso/%s.latest.iso", IMAGE_NAME);
        print_color(COLOR_GREEN, "Starting QEMU with %s ...", iso_path);
        cmd_run(false,
            "GDK_BACKEND=x11 qemu-system-x86_64 -machine pc"
            " -cdrom %s -boot d"
            " -serial stdio"
            " %s"
            " 2>&1 | tee log.txt",
            iso_path, QEMUFLAGS);

        if (!ARG_NO_CLEAN) {
            print_color(COLOR_CYAN, "[post-run] Cleaning build artifacts...");
            rm_rf("obj");
            rm_rf("bin");
            rm_rf(INITRAMFS_ROOTFS);
            if (file_exists(INITRAMFS_TAR)) {
                remove(INITRAMFS_TAR);
                print_color(COLOR_GREEN, "[post-run] removed %s", INITRAMFS_TAR);
            }
            clean_apps_elfs();
            print_color(COLOR_GREEN, "[post-run] Done.");
        }
        return 0;
    }

    print_color(COLOR_RED, "Unknown command: %s (try: ./build help)", command);
    return 1;
}