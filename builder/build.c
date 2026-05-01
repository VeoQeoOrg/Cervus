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
#define VERSION      "v0.0.2"
#define QEMUFLAGS    "-m 8G -smp 8 -cpu qemu64,+fsgsbase -display gtk,grab-on-hover=on " \
                     "-drive file=cervus_disk.img,format=raw,if=ide,index=0,media=disk "

#define LIMINE_CONF_PATH       "/boot/limine/limine.conf"
#define LIMINE_CONF_BACKUP     "/boot/limine/limine.conf.cervus-backup"
#define CERVUS_BOOT_DIR        "/boot/cervus"
#define CERVUS_MARKER          "# --- CERVUS OS ENTRY ---"

#define WALLPAPER_SRC "wallpapers/cervus1280x720.png"
#define WALLPAPER_DST "boot():/boot/wallpapers/cervus.png"

#define APPS_DIR         "usr/apps"
#define BIN_APPS_DIR     "usr/bin"
#define INSTALLER_DIR    "usr/installer"
#define SYSROOT_DIR      "usr/sysroot"
#define SYSROOT_INC      "usr/sysroot/usr/include"
#define SYSROOT_LIB      "usr/sysroot/usr/lib"
#define LIBCERVUS_DIR    "usr/lib/libcervus"
#define SHELL_SRC        "usr/apps/shell.c"
#define SHELL_ELF        "usr/apps/shell.elf"
#define INSTALLER_SRC    "usr/installer/install-on-disk.c"
#define INSTALLER_ELF    "usr/installer/install-on-disk.elf"

#define INITRAMFS_TAR      "initramfs.tar"
#define INITRAMFS_ROOTFS   "rootfs"

#define BIGPATH (PATH_MAX * 2)

#define SHORTPATH 512

static void path_join2(char *dst, size_t dst_size, const char *a, const char *b) {
    if (dst_size == 0) return;
    size_t la = strlen(a);
    size_t lb = strlen(b);
    if (la >= dst_size) la = dst_size - 1;
    memcpy(dst, a, la);
    size_t off = la;
    if (off + 1 < dst_size) dst[off++] = '/';
    if (lb > dst_size - off - 1) lb = dst_size - off - 1;
    memcpy(dst + off, b, lb);
    dst[off + lb] = '\0';
}

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
    SHELL_ELF, INSTALLER_ELF,
    SYSROOT_LIB "/libcervus.a",
    SYSROOT_LIB "/crt0.o",
    SYSROOT_DIR "/usr/bin/tcc",
    SYSROOT_LIB "/tcc/libtcc1.a",
    INITRAMFS_TAR,
    "cervus_disk.img",
    NULL
};

const char *SSE_FILES[] = {
    "sse.c", "fpu.c", "fabs.c", "pow.c", "pow10.c",
    "serial.c", "snprintf.c", "printf.c", NULL
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
bool ARG_RESET_HW_CONF  = false;
bool ARG_RESET_DISK     = false;
bool ARG_LIVE           = false;

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
    int r = system(tmp);
    (void)r;
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

static bool extract_limine_hdd_bin(void) {
    const char *src = "limine/limine-bios-hdd.h";
    const char *dst = "limine/limine-bios-hdd.bin";

    if (file_exists(dst) && file_exists(src) && get_mtime(src) <= get_mtime(dst)) {
        return true;
    }
    if (!file_exists(src)) {
        print_color(COLOR_RED, "limine-bios-hdd.h not found in limine/");
        return false;
    }

    FILE *in = fopen(src, "r");
    if (!in) {
        print_color(COLOR_RED, "cannot open %s", src);
        return false;
    }
    FILE *out = fopen(dst, "wb");
    if (!out) {
        fclose(in);
        print_color(COLOR_RED, "cannot create %s", dst);
        return false;
    }

    char line[4096];
    size_t total = 0;
    while (fgets(line, sizeof(line), in)) {
        char *p = line;
        while (*p) {
            if (p[0] == '0' && p[1] == 'x') {
                unsigned v = 0;
                p += 2;
                if (sscanf(p, "%2x", &v) == 1) {
                    unsigned char b = (unsigned char)v;
                    fwrite(&b, 1, 1, out);
                    total++;
                    p += 2;
                } else {
                    p++;
                }
            } else {
                p++;
            }
        }
    }
    fclose(in);
    fclose(out);
    print_color(COLOR_GREEN, "Extracted limine-bios-hdd.bin (%zu bytes)", total);
    return true;
}

bool build_limine(void) {
    if (file_exists("limine/limine")) {
        print_color(COLOR_GREEN, "Limine already built");
        extract_limine_hdd_bin();
        return true;
    }
    print_color(COLOR_GREEN, "Building Limine...");
    if (file_exists("limine")) rm_rf("limine");
    if (cmd_run(true, "git clone https://codeberg.org/Limine/Limine.git limine "
                      "--branch=v11.2.1-binary --depth=1") != 0) return false;
    if (cmd_run(true, "make -C limine") != 0) return false;
    if (!extract_limine_hdd_bin()) return false;
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
        " -fno-pie -fno-pic"
        " -mno-sse -mno-sse2 -mno-mmx -mno-avx -mno-avx2"
        " -mno-red-zone"
        " -O0 -g"
        " -nostdinc -isystem " SYSROOT_INC
        " -Wl,-Ttext-segment=0x401000"
        " -o %s %s " SYSROOT_LIB "/crt0.o " SYSROOT_LIB "/libcervus.a",
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

static app_entry_t g_bin_apps[MAX_APPS];
static int         g_nbin = 0;

static int scan_bin_apps(void) {
    g_nbin = 0;
    DIR *d = opendir(BIN_APPS_DIR);
    if (!d) {
        print_color(COLOR_YELLOW, "[bin] No '%s' directory found, skipping", BIN_APPS_DIR);
        return 0;
    }
    struct dirent *de;
    while ((de = readdir(d)) != NULL && g_nbin < MAX_APPS) {
        const char *nm = de->d_name;
        size_t nlen = strlen(nm);
        if (nlen < 3) continue;
        if (nm[0] == '.') continue;
        if (strcmp(nm + nlen - 2, ".c") != 0) continue;

        app_entry_t *e = &g_bin_apps[g_nbin];
        snprintf(e->src,  sizeof(e->src),  "%s/%s",       BIN_APPS_DIR, nm);
        snprintf(e->elf,  sizeof(e->elf),  "%s/%.*s.elf", BIN_APPS_DIR, (int)(nlen-2), nm);
        snprintf(e->name, sizeof(e->name), "%.*s",         (int)(nlen-2), nm);
        g_nbin++;
    }
    closedir(d);
    for (int i = 1; i < g_nbin; i++) {
        app_entry_t tmp = g_bin_apps[i]; int j = i-1;
        while (j >= 0 && strcmp(g_bin_apps[j].name, tmp.name) > 0) {
            g_bin_apps[j+1] = g_bin_apps[j]; j--;
        }
        g_bin_apps[j+1] = tmp;
    }
    return g_nbin;
}

static bool build_one_bin_app(const app_entry_t *e) {
    if (file_exists(e->elf) && get_mtime(e->src) <= get_mtime(e->elf)) {
        print_color(COLOR_GREEN, "[bin] %s is up to date", e->elf);
        return true;
    }
    print_color(COLOR_CYAN, "[bin] Compiling %s -> %s", e->src, e->elf);

    int ret = cmd_run(false,
        "gcc -ffreestanding -nostdlib -static -fno-stack-protector"
        " -fno-pie -fno-pic"
        " -mno-sse -mno-sse2 -mno-mmx -mno-avx -mno-avx2"
        " -mno-red-zone"
        " -O0 -g"
        " -nostdinc -isystem " SYSROOT_INC
        " -Wl,-Ttext-segment=0x401000"
        " -o %s %s " SYSROOT_LIB "/crt0.o " SYSROOT_LIB "/libcervus.a",
        e->elf, e->src);
    if (ret != 0) {
        print_color(COLOR_RED, "[bin] Failed to compile %s", e->src);
        return false;
    }
    print_color(COLOR_GREEN, "[bin] %s built successfully", e->elf);
    return true;
}

bool build_all_bin_apps(void) {
    scan_bin_apps();
    if (g_nbin == 0) {
        print_color(COLOR_YELLOW, "[bin] No .c files found in '%s'", BIN_APPS_DIR);
        return true;
    }
    print_color(COLOR_CYAN, "[bin] Found %d program(s) in '%s'", g_nbin, BIN_APPS_DIR);
    for (int i = 0; i < g_nbin; i++) {
        if (!build_one_bin_app(&g_bin_apps[i])) return false;
    }
    return true;
}

void clean_bin_elfs(void) {
    DIR *d = opendir(BIN_APPS_DIR);
    if (!d) return;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        const char *nm = de->d_name;
        size_t nlen = strlen(nm);
        if (nlen < 5) continue;
        if (strcmp(nm + nlen - 4, ".elf") != 0) continue;
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", BIN_APPS_DIR, nm);
        remove(path);
        print_color(COLOR_YELLOW, "[clean] removed %s", path);
    }
    closedir(d);
}

#define LCV_CFLAGS_INT  "-ffreestanding -nostdlib -static -fno-stack-protector " \
                        "-mno-sse -mno-sse2 -mno-mmx -mno-avx -mno-avx2 " \
                        "-mno-red-zone -fno-pie -fno-pic " \
                        "-O0 -g -Wall -Wextra"

#define LCV_CFLAGS_FLT  "-ffreestanding -nostdlib -static -fno-stack-protector " \
                        "-mno-red-zone -fno-pie -fno-pic " \
                        "-O0 -g -Wall -Wextra"

typedef struct {
    const char *src;
    const char *obj;
    int         is_float;
} libcervus_unit_t;

static const libcervus_unit_t LCV_UNITS[] = {
    { "libcervus.c",          "libcervus.o",         0 },
    { "compat.c",             "compat.o",            0 },
    { "ctype/ctype.c",        "ctype/ctype.o",       0 },
    { "string/string.c",      "string/string.o",     0 },
    { "memory/memory.c",      "memory/memory.o",     0 },
    { "stdlib/stdlib.c",      "stdlib/stdlib.o",     0 },
    { "stdlib/strtod.c",      "stdlib/strtod.o",     1 },
    { "stdio/stdio.c",        "stdio/stdio.o",       1 },
    { "stdio/scanf.c",        "stdio/scanf.o",       1 },
    { "time/time.c",          "time/time.o",         0 },
    { "signal/signal.c",      "signal/signal.o",     0 },
    { "dirent/dirent.c",      "dirent/dirent.o",     0 },
    { "math/abs.c",           "math/abs.o",          0 },
    { "math/fabs.c",          "math/fabs.o",         1 },
    { "math/isinf.c",         "math/isinf.o",        0 },
    { "math/isnan.c",         "math/isnan.o",        0 },
    { "math/pow.c",           "math/pow.o",          1 },
    { "math/pow10.c",         "math/pow10.o",        1 },
    { NULL, NULL, 0 }
};

static long get_mtime_safe(const char *p) {
    struct stat st;
    if (stat(p, &st) != 0) return 0;
    return (long)st.st_mtime;
}

static bool need_rebuild(const char *src, const char *obj) {
    if (!file_exists(obj)) return true;
    return get_mtime_safe(src) > get_mtime_safe(obj);
}

bool build_libcervus(void) {
    if (cmd_run(false, "command -v nasm >/dev/null 2>&1") != 0) {
        print_color(COLOR_RED,
            "[libcervus] 'nasm' is required to build crt0.o/setjmp.o. Install: "
            "apt install nasm | dnf install nasm | pacman -S nasm");
        return false;
    }
    if (cmd_run(false, "command -v ar >/dev/null 2>&1") != 0) {
        print_color(COLOR_RED, "[libcervus] 'ar' (binutils) is required");
        return false;
    }

    print_color(COLOR_CYAN, "[libcervus] Building libcervus.a and crt0.o (native)...");

    char incdir[BIGPATH];
    char libdir[BIGPATH];
    char cwd_buf[PATH_MAX];
    if (!getcwd(cwd_buf, sizeof(cwd_buf))) {
        print_color(COLOR_RED, "[libcervus] getcwd failed");
        return false;
    }
    path_join2(incdir, sizeof(incdir), cwd_buf, SYSROOT_INC);
    path_join2(libdir, sizeof(libdir), cwd_buf, SYSROOT_LIB);

    int rc = chdir(LIBCERVUS_DIR);
    if (rc != 0) {
        print_color(COLOR_RED, "[libcervus] cannot chdir to %s", LIBCERVUS_DIR);
        return false;
    }

    bool any_changed = false;
    int  built       = 0;
    int  skipped     = 0;

    for (int i = 0; LCV_UNITS[i].src; i++) {
        const libcervus_unit_t *u = &LCV_UNITS[i];
        if (!need_rebuild(u->src, u->obj)) { skipped++; continue; }
        const char *cflags = u->is_float ? LCV_CFLAGS_FLT : LCV_CFLAGS_INT;
        if (cmd_run(false, "gcc %s -nostdinc -isystem %s -c -o %s %s",
                    cflags, incdir, u->obj, u->src) != 0) {
            print_color(COLOR_RED, "[libcervus] failed to compile %s", u->src);
            (void)!chdir(cwd_buf);
            return false;
        }
        any_changed = true;
        built++;
    }

    if (need_rebuild("setjmp.asm", "setjmp.o")) {
        if (cmd_run(false, "nasm -f elf64 -o setjmp.o setjmp.asm") != 0) {
            print_color(COLOR_RED, "[libcervus] nasm failed on setjmp.asm");
            (void)!chdir(cwd_buf); return false;
        }
        any_changed = true; built++;
    } else skipped++;

    if (need_rebuild("crt0.asm", "crt0.o")) {
        if (cmd_run(false, "nasm -f elf64 -o crt0.o crt0.asm") != 0) {
            print_color(COLOR_RED, "[libcervus] nasm failed on crt0.asm");
            (void)!chdir(cwd_buf); return false;
        }
        any_changed = true; built++;
    } else skipped++;

    bool need_ar = any_changed || !file_exists("libcervus.a");
    if (need_ar) {
        char ar_cmd[8192];
        size_t pos = 0;
        pos += snprintf(ar_cmd + pos, sizeof(ar_cmd) - pos, "ar rcs libcervus.a");
        for (int i = 0; LCV_UNITS[i].src; i++) {
            pos += snprintf(ar_cmd + pos, sizeof(ar_cmd) - pos, " %s", LCV_UNITS[i].obj);
        }
        pos += snprintf(ar_cmd + pos, sizeof(ar_cmd) - pos, " setjmp.o");
        if (cmd_run(false, "%s", ar_cmd) != 0) {
            print_color(COLOR_RED, "[libcervus] ar failed");
            (void)!chdir(cwd_buf); return false;
        }
    }

    if (cmd_run(false, "mkdir -p %s", libdir) != 0 ||
        cmd_run(false, "cp libcervus.a %s/", libdir) != 0 ||
        cmd_run(false, "cp crt0.o %s/", libdir) != 0)
    {
        print_color(COLOR_RED, "[libcervus] install copy failed");
        (void)!chdir(cwd_buf); return false;
    }

    (void)!chdir(cwd_buf);
    print_color(COLOR_GREEN, "[libcervus] OK (compiled %d, skipped %d)", built, skipped);
    return true;
}

#define TCC_VERSION_STR    "0.9.27"
#define TCC_TAR_FNAME      "tcc-0.9.27.tar.bz2"
#define TCC_SRC_DIR        "tcc-0.9.27"
#define TCC_DOWNLOAD_URL   "https://download.savannah.gnu.org/releases/tinycc/tcc-0.9.27.tar.bz2"
#define TCC_DIR            "usr/tcc"

typedef struct { char *data; size_t len; size_t cap; } dstr_t;

static void dstr_init(dstr_t *s) { s->data = NULL; s->len = 0; s->cap = 0; }
static void dstr_free(dstr_t *s) { free(s->data); s->data = NULL; s->len = s->cap = 0; }

static bool dstr_reserve(dstr_t *s, size_t need) {
    if (s->cap >= need) return true;
    size_t nc = s->cap ? s->cap : 256;
    while (nc < need) nc *= 2;
    char *p = realloc(s->data, nc);
    if (!p) return false;
    s->data = p; s->cap = nc;
    return true;
}

static bool read_file_dstr(const char *path, dstr_t *out) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) { fclose(f); return false; }
    if (!dstr_reserve(out, (size_t)sz + 1)) { fclose(f); return false; }
    if (sz > 0 && fread(out->data, 1, (size_t)sz, f) != (size_t)sz) {
        fclose(f); return false;
    }
    out->len = (size_t)sz;
    out->data[out->len] = '\0';
    fclose(f);
    return true;
}

static bool write_file_dstr(const char *path, const dstr_t *s) {
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    if (s->len > 0 && fwrite(s->data, 1, s->len, f) != s->len) { fclose(f); return false; }
    fclose(f);
    return true;
}

static bool dstr_replace_once(dstr_t *s, const char *old_s, const char *new_s) {
    size_t old_n = strlen(old_s);
    size_t new_n = strlen(new_s);
    if (old_n == 0 || s->len < old_n) return false;
    char *p = memmem(s->data, s->len, old_s, old_n);
    if (!p) return false;
    size_t off = (size_t)(p - s->data);

    if (new_n > old_n) {
        if (!dstr_reserve(s, s->len + (new_n - old_n) + 1)) return false;
        p = s->data + off;
    }

    memmove(p + new_n, p + old_n, s->len - off - old_n + 1 );
    memcpy(p, new_s, new_n);
    s->len += (new_n - old_n);
    return true;
}

static bool dstr_guard_line_with(dstr_t *s, const char *marker, const char *guard_macro) {
    char *p = memmem(s->data, s->len, marker, strlen(marker));
    if (!p) return false;

    char *line_start = p;
    while (line_start > s->data && line_start[-1] != '\n') line_start--;

    char *prev_line_start = line_start;
    if (prev_line_start > s->data) prev_line_start--;
    while (prev_line_start > s->data && prev_line_start[-1] != '\n') prev_line_start--;

    char needle[160];
    snprintf(needle, sizeof(needle), "#ifndef %s", guard_macro);
    size_t pll = (size_t)(line_start - prev_line_start);
    if (pll >= strlen(needle) &&
        memcmp(prev_line_start, needle, strlen(needle)) == 0)
    {
        return false;
    }

    char *line_end = p;
    while ((size_t)(line_end - s->data) < s->len && *line_end != '\n') line_end++;
    if ((size_t)(line_end - s->data) < s->len && *line_end == '\n') line_end++;

    char prefix[160], suffix[16];
    snprintf(prefix, sizeof(prefix), "#ifndef %s\n", guard_macro);
    snprintf(suffix, sizeof(suffix), "#endif\n");
    size_t ins_pre = strlen(prefix), ins_suf = strlen(suffix);
    size_t off_start = (size_t)(line_start - s->data);
    size_t off_end   = (size_t)(line_end   - s->data);

    if (!dstr_reserve(s, s->len + ins_pre + ins_suf + 1)) return false;
    line_start = s->data + off_start;
    line_end   = s->data + off_end;

    memmove(line_end + ins_pre + ins_suf, line_end, s->len - off_end + 1);

    memcpy(line_end + ins_pre, suffix, ins_suf);

    memmove(line_start + ins_pre, line_start, off_end - off_start);

    memcpy(line_start, prefix, ins_pre);
    s->len += ins_pre + ins_suf;
    return true;
}

static int dstr_guard_func_impls(dstr_t *s, const char *name, const char *guard_macro) {
    int count = 0;
    size_t cursor = 0;
    size_t name_n = strlen(name);

    while (cursor + name_n < s->len) {
        char *p = memmem(s->data + cursor, s->len - cursor, name, name_n);
        if (!p) break;
        size_t pos = (size_t)(p - s->data);

        size_t i = pos + name_n;
        while (i < s->len && (s->data[i] == ' ' || s->data[i] == '\t')) i++;
        if (i >= s->len || s->data[i] != '(') {
            cursor = pos + 1; continue;
        }

        size_t j = i;
        while (j < s->len && s->data[j] != '{' && s->data[j] != ';') j++;
        if (j >= s->len) break;
        if (s->data[j] == ';') { cursor = pos + 1; continue; }

        int depth = 0;
        size_t end = j;
        for (; end < s->len; end++) {
            if (s->data[end] == '{') depth++;
            else if (s->data[end] == '}') { depth--; if (depth == 0) { end++; break; } }
        }
        if (depth != 0) break;
        if (end < s->len && s->data[end] == '\n') end++;

        size_t line_start = pos;
        while (line_start > 0 && s->data[line_start - 1] != '\n') line_start--;

        size_t look = line_start > 200 ? line_start - 200 : 0;
        if (memmem(s->data + look, line_start - look,
                   guard_macro, strlen(guard_macro))) {
            cursor = end; continue;
        }

        char prefix[160], suffix[16];
        snprintf(prefix, sizeof(prefix), "#ifndef %s\n", guard_macro);
        snprintf(suffix, sizeof(suffix), "#endif\n");
        size_t pre_n = strlen(prefix), suf_n = strlen(suffix);
        if (!dstr_reserve(s, s->len + pre_n + suf_n + 1)) return count;

        memmove(s->data + end + pre_n + suf_n,
                s->data + end,
                s->len - end + 1);
        memcpy(s->data + end + pre_n, suffix, suf_n);
        memmove(s->data + line_start + pre_n,
                s->data + line_start,
                end - line_start);
        memcpy(s->data + line_start, prefix, pre_n);
        s->len += pre_n + suf_n;
        count++;
        cursor = end + pre_n + suf_n;
    }
    return count;
}

static bool patch_file(const char *src_dir, const char *rel,
                       int (*editor)(dstr_t *)) {
    char path[SHORTPATH];
    path_join2(path, sizeof(path), src_dir, rel);
    dstr_t s; dstr_init(&s);
    if (!read_file_dstr(path, &s)) {
        print_color(COLOR_RED, "[tcc-patch] cannot read %s", path);
        return false;
    }
    int changed = editor(&s);
    if (changed > 0) {
        if (!write_file_dstr(path, &s)) {
            print_color(COLOR_RED, "[tcc-patch] cannot write %s", path);
            dstr_free(&s); return false;
        }
        print_color(COLOR_GREEN, "[tcc-patch] %s: %d edit(s)", rel, changed);
    } else {
        print_color(COLOR_YELLOW, "[tcc-patch] %s: no changes (already patched?)", rel);
    }
    dstr_free(&s);
    return true;
}

static int edit_tcc_h(dstr_t *s) {
    int changed = 0;
    if (dstr_guard_line_with(s, "#include <dlfcn.h>", "TCC_NO_DLOPEN")) changed++;

    {
        const char *needle = "#include <sys/utsname.h>";
        char *p = memmem(s->data, s->len, needle, strlen(needle));
        if (p && !memmem(s->data, s->len,
                          "#include <sys/mman.h>",
                          strlen("#include <sys/mman.h>"))) {
            const char *ins = "#include <sys/mman.h>\n";
            size_t off = (size_t)(p - s->data);
            size_t ins_n = strlen(ins);
            if (!dstr_reserve(s, s->len + ins_n + 1)) return changed;
            memmove(s->data + off + ins_n, s->data + off, s->len - off + 1);
            memcpy(s->data + off, ins, ins_n);
            s->len += ins_n;
            changed++;
        }
    }

    if (dstr_guard_line_with(s, "enable bound checking code", "CONFIG_TCC_BCHECK")) changed++;
    return changed;
}

static int edit_libtcc_c(dstr_t *s) {
    int changed = 0;

    if (dstr_replace_once(s,
        "return dlopen(filename, RTLD_GLOBAL | RTLD_LAZY);\n",
        "(void)filename;\n    return NULL;\n")) changed++;
    if (dstr_replace_once(s,
        "return dlopen(filename, RTLD_LOCAL | RTLD_LAZY);\n",
        "(void)filename;\n    return NULL;\n")) changed++;
    if (dstr_replace_once(s,
        "return dlsym(handle, sym);\n",
        "(void)handle; (void)sym;\n    return NULL;\n")) changed++;
    if (dstr_replace_once(s,
        "dlclose(handle);\n",
        "(void)handle;\n")) changed++;

    if (!memmem(s->data, s->len, "Cervus: no ld.so", 16)) {
        if (dstr_replace_once(s,
            "    s->alacarte_link = 1;",
            "    s->alacarte_link = 1;\n    s->static_link = 1;  /* Cervus: no ld.so */"))
            changed++;
    }

    if (dstr_replace_once(s,
        "        if (output_type != TCC_OUTPUT_DLL)\n"
        "            tcc_add_crt(s, \"crt1.o\");\n"
        "        tcc_add_crt(s, \"crti.o\");",
        "        if (output_type != TCC_OUTPUT_DLL)\n"
        "            tcc_add_crt(s, \"crt0.o\");  /* Cervus: only crt0 */"))
        changed++;
    return changed;
}

static int edit_tccrun_c(dstr_t *s) {
    int changed = 0;

    if (dstr_guard_line_with(s, "<sys/ucontext.h>", "TCC_NO_BACKTRACE")) changed++;

    if (!memmem(s->data, s->len, "typedef void *ucontext_t;",
                strlen("typedef void *ucontext_t;")))
    {

        size_t scan = s->len < 4096 ? s->len : 4096;
        size_t last_inc_end = 0;
        for (size_t i = 0; i < scan; ) {
            if (s->data[i] == '#' && i + 8 <= s->len &&
                memcmp(s->data + i, "#include", 8) == 0)
            {

                size_t e = i;
                while (e < s->len && s->data[e] != '\n') e++;
                if (e < s->len) e++;
                last_inc_end = e;
                i = e; continue;
            }
            i++;
        }
        if (last_inc_end > 0) {
            const char *stub = "\n#ifdef TCC_NO_BACKTRACE\n"
                               "typedef void *ucontext_t;\n"
                               "#endif\n";
            size_t stub_n = strlen(stub);
            if (dstr_reserve(s, s->len + stub_n + 1)) {
                memmove(s->data + last_inc_end + stub_n,
                        s->data + last_inc_end,
                        s->len - last_inc_end + 1);
                memcpy(s->data + last_inc_end, stub, stub_n);
                s->len += stub_n;
                changed++;
            }
        }
    }

    if (dstr_guard_line_with(s, "set_exception_handler();", "TCC_NO_BACKTRACE")) changed++;

    const char *names[] = { "rt_error", "sig_error",
                            "set_exception_handler", "rt_get_caller_pc", NULL };
    for (int i = 0; names[i]; i++) {
        int n = dstr_guard_func_impls(s, names[i], "TCC_NO_BACKTRACE");
        if (n > 0) changed++;
    }
    return changed;
}

static int edit_tccelf_c(dstr_t *s) {
    int changed = 0;

    if (!memmem(s->data, s->len, "Cervus: -lcervus", 16)) {
        if (dstr_replace_once(s,
            "    if (!s1->nostdlib) {\n        tcc_add_library_err(s1, \"c\");",
            "    if (!s1->nostdlib) {\n        tcc_add_library_err(s1, \"cervus\");  /* Cervus: -lcervus */"))
            changed++;
    }

    if (dstr_replace_once(s,
        "        if (s1->output_type != TCC_OUTPUT_MEMORY)\n"
        "            tcc_add_crt(s1, \"crtn.o\");\n"
        "    }\n",
        "        /* Cervus: no crtn.o */\n    }\n"))
        changed++;
    return changed;
}

static bool write_tcc_config_h(const char *src_dir) {
    char path[SHORTPATH];
    path_join2(path, sizeof(path), src_dir, "config.h");
    FILE *f = fopen(path, "w");
    if (!f) return false;
    fputs(
        "#ifndef _CONFIG_H\n"
        "#define _CONFIG_H\n\n"
        "#define TCC_VERSION \"" TCC_VERSION_STR "\"\n\n"
        "#define CONFIG_TCC_SYSROOT      \"\"\n"
        "#define CONFIG_TCC_LIBPATHS     \"/usr/lib\"\n"
        "#define CONFIG_TCC_CRTPREFIX    \"/usr/lib\"\n"
        "#define CONFIG_TCC_ELFINTERP    \"\"\n"
        "#define CONFIG_TCCDIR           \"/usr/lib/tcc\"\n\n"
        "#define HOST_OS    \"Cervus\"\n"
        "#define HOST_ARCH  \"x86_64\"\n\n"
        "#define TCC_TARGET_X86_64 1\n\n"
        "#define CONFIG_TCC_PREDEFS  1\n"
        "#define CONFIG_TCC_STATIC   1\n\n"
        "#define TCC_NO_DLOPEN    1\n"
        "#define TCC_NO_BACKTRACE 1\n\n"
        "#define CONFIG_TCC_BCHECK 0\n\n"
        "#undef  CONFIG_WIN32\n"
        "#undef  CONFIG_WIN64\n"
        "#undef  TCC_TARGET_PE\n\n"
        "#endif /* _CONFIG_H */\n",
        f);
    fclose(f);
    return true;
}

static bool tcc_download(void) {
    char tar_path[SHORTPATH];
    path_join2(tar_path, sizeof(tar_path), TCC_DIR, TCC_TAR_FNAME);
    if (file_exists(tar_path)) return true;
    print_color(COLOR_CYAN, "[tcc] Downloading %s...", TCC_TAR_FNAME);
    if (cmd_run(false, "command -v wget >/dev/null 2>&1") == 0) {
        if (cmd_run(true, "wget -q -O %s %s", tar_path, TCC_DOWNLOAD_URL) == 0
            && file_exists(tar_path)) return true;
    }
    if (cmd_run(false, "command -v curl >/dev/null 2>&1") == 0) {
        if (cmd_run(true, "curl -fL -o %s %s", tar_path, TCC_DOWNLOAD_URL) == 0
            && file_exists(tar_path)) return true;
    }
    print_color(COLOR_RED, "[tcc] cannot download (need wget or curl)");
    return false;
}

static bool tcc_extract_and_patch(void) {
    char src_dir_full[SHORTPATH];
    path_join2(src_dir_full, sizeof(src_dir_full), TCC_DIR, TCC_SRC_DIR);

    if (!file_exists(src_dir_full)) {
        print_color(COLOR_CYAN, "[tcc] Extracting %s...", TCC_TAR_FNAME);
        if (cmd_run(true, "tar -xjf %s/%s -C %s",
                    TCC_DIR, TCC_TAR_FNAME, TCC_DIR) != 0) {
            print_color(COLOR_RED, "[tcc] tar -xjf failed");
            return false;
        }
    }

    if (!write_tcc_config_h(src_dir_full)) {
        print_color(COLOR_RED, "[tcc] cannot write config.h");
        return false;
    }

    print_color(COLOR_CYAN, "[tcc] Applying Cervus patches (idempotent)...");
    if (!patch_file(src_dir_full, "tcc.h",     edit_tcc_h))     return false;
    if (!patch_file(src_dir_full, "libtcc.c",  edit_libtcc_c))  return false;
    if (!patch_file(src_dir_full, "tccrun.c",  edit_tccrun_c))  return false;
    if (!patch_file(src_dir_full, "tccelf.c",  edit_tccelf_c))  return false;
    return true;
}

static bool tcc_build_and_install(void) {
    char src_dir_full[SHORTPATH];
    path_join2(src_dir_full, sizeof(src_dir_full), TCC_DIR, TCC_SRC_DIR);

    char cwd_buf[PATH_MAX];
    if (!getcwd(cwd_buf, sizeof(cwd_buf))) {
        print_color(COLOR_RED, "[tcc] getcwd failed");
        return false;
    }
    char incdir[BIGPATH], libdir[BIGPATH];
    path_join2(incdir, sizeof(incdir), cwd_buf, SYSROOT_INC);
    path_join2(libdir, sizeof(libdir), cwd_buf, SYSROOT_LIB);

    char tcc_elf[SHORTPATH], libtcc1_a[SHORTPATH];
    path_join2(tcc_elf,   sizeof(tcc_elf),   TCC_DIR, "tcc.elf");
    path_join2(libtcc1_a, sizeof(libtcc1_a), TCC_DIR, "libtcc1.a");

    char one_src[SHORTPATH];
    path_join2(one_src, sizeof(one_src), src_dir_full, "tcc.c");

    char one_hdr[SHORTPATH], one_libtcc[SHORTPATH];
    path_join2(one_hdr,    sizeof(one_hdr),    src_dir_full, "tcc.h");
    path_join2(one_libtcc, sizeof(one_libtcc), src_dir_full, "libtcc.c");

    long elf_t = get_mtime_safe(tcc_elf);
    bool need_tcc =
        !file_exists(tcc_elf) ||
        get_mtime_safe(one_src)    > elf_t ||
        get_mtime_safe(one_hdr)    > elf_t ||
        get_mtime_safe(one_libtcc) > elf_t;

    if (need_tcc) {
        print_color(COLOR_CYAN, "[tcc] Building tcc.elf for Cervus (x86_64)...");
        char crt0[BIGPATH];
        path_join2(crt0, sizeof(crt0), libdir, "crt0.o");
        if (!file_exists(crt0)) {
            print_color(COLOR_RED, "[tcc] %s missing (libcervus build failed?)", crt0);
            return false;
        }
        int rc = cmd_run(true,
            "gcc -ffreestanding -nostdlib -static -fno-stack-protector "
            "-mno-red-zone -fno-pie -fno-pic -O2 -D__CERVUS__ "
            "-nostdinc -isystem %s "
            "-DTCC_TARGET_X86_64 -DONE_SOURCE=1 "
            "-DCONFIG_TCC_PREDEFS=1 -DTCC_NO_DLOPEN=1 -DTCC_NO_BACKTRACE=1 "
            "-DCONFIG_TCC_STATIC=1 "
            "-I%s "
            "-o %s %s/tcc.c %s "
            "-nostdlib -static -L%s -lcervus",
            incdir, src_dir_full, tcc_elf, src_dir_full, crt0, libdir);
        if (rc != 0) {
            print_color(COLOR_RED, "[tcc] tcc.elf build failed");
            return false;
        }
    } else {
        print_color(COLOR_GREEN, "[tcc] tcc.elf up to date");
    }

    char l1_o[SHORTPATH], al_o[SHORTPATH], va_o[SHORTPATH];
    path_join2(l1_o, sizeof(l1_o), TCC_DIR, "libtcc1.o");
    path_join2(al_o, sizeof(al_o), TCC_DIR, "alloca86_64.o");
    path_join2(va_o, sizeof(va_o), TCC_DIR, "va_list.o");

    bool need_libtcc1 = !file_exists(libtcc1_a);
    if (need_libtcc1) {
        print_color(COLOR_CYAN, "[tcc] Building libtcc1.a...");
        const char *l1_flags =
            "-ffreestanding -nostdlib -fno-stack-protector "
            "-mno-red-zone -fno-pie -fno-pic -O2 -D__CERVUS__ "
            "-DTCC_TARGET_X86_64";

        char src_path[SHORTPATH];
        char ar_cmd[2048];
        size_t arpos = (size_t)snprintf(ar_cmd, sizeof(ar_cmd),
                                        "ar rcs %s", libtcc1_a);

        path_join2(src_path, sizeof(src_path), src_dir_full, "lib/libtcc1.c");
        if (!file_exists(src_path)) {
            print_color(COLOR_RED, "[tcc] missing %s", src_path);
            return false;
        }
        if (cmd_run(true, "gcc %s -c %s -o %s", l1_flags, src_path, l1_o) != 0) {
            print_color(COLOR_RED, "[tcc] libtcc1.c compile failed"); return false;
        }
        arpos += (size_t)snprintf(ar_cmd + arpos, sizeof(ar_cmd) - arpos, " %s", l1_o);

        path_join2(src_path, sizeof(src_path), src_dir_full, "lib/alloca86_64.S");
        if (file_exists(src_path)) {
            if (cmd_run(true, "gcc %s -c %s -o %s", l1_flags, src_path, al_o) != 0) {
                print_color(COLOR_RED, "[tcc] alloca86_64.S compile failed"); return false;
            }
            arpos += (size_t)snprintf(ar_cmd + arpos, sizeof(ar_cmd) - arpos, " %s", al_o);
        }

        path_join2(src_path, sizeof(src_path), src_dir_full, "lib/va_list.c");
        if (file_exists(src_path)) {
            if (cmd_run(true, "gcc %s -c %s -o %s", l1_flags, src_path, va_o) != 0) {
                print_color(COLOR_RED, "[tcc] va_list.c compile failed"); return false;
            }
            arpos += (size_t)snprintf(ar_cmd + arpos, sizeof(ar_cmd) - arpos, " %s", va_o);
        }

        if (cmd_run(true, "%s", ar_cmd) != 0) {
            print_color(COLOR_RED, "[tcc] ar failed"); return false;
        }
    } else {
        print_color(COLOR_GREEN, "[tcc] libtcc1.a up to date");
    }

    print_color(COLOR_CYAN, "[tcc] Installing to sysroot...");
    if (cmd_run(false, "mkdir -p %s/usr/bin %s/usr/lib/tcc/include",
                SYSROOT_DIR, SYSROOT_DIR) != 0) {
        print_color(COLOR_RED, "[tcc] mkdir failed");
        return false;
    }
    if (cmd_run(false, "cp %s %s/usr/bin/tcc", tcc_elf, SYSROOT_DIR) != 0 ||
        cmd_run(false, "cp %s %s/usr/lib/tcc/libtcc1.a", libtcc1_a, SYSROOT_DIR) != 0 ||
        cmd_run(false, "cp %s/include/*.h %s/usr/lib/tcc/include/",
                src_dir_full, SYSROOT_DIR) != 0)
    {
        print_color(COLOR_RED, "[tcc] install copy failed");
        return false;
    }

    print_color(COLOR_GREEN, "[tcc] Installed to %s/usr/bin/tcc", SYSROOT_DIR);
    return true;
}

bool build_tcc(void) {
    if (!tcc_download())          return false;
    if (!tcc_extract_and_patch()) return false;
    if (!tcc_build_and_install()) return false;
    return true;
}

void clean_tcc_build(bool deep) {
    char tcc_elf[PATH_MAX], libtcc1_a[PATH_MAX];
    path_join2(tcc_elf,   sizeof(tcc_elf),   TCC_DIR, "tcc.elf");
    path_join2(libtcc1_a, sizeof(libtcc1_a), TCC_DIR, "libtcc1.a");

    cmd_run(false, "rm -f %s/libtcc1.o %s/alloca86_64.o %s/va_list.o",
            TCC_DIR, TCC_DIR, TCC_DIR);
    if (file_exists(tcc_elf))   { remove(tcc_elf);   print_color(COLOR_YELLOW, "[clean] removed %s",   tcc_elf); }
    if (file_exists(libtcc1_a)) { remove(libtcc1_a); print_color(COLOR_YELLOW, "[clean] removed %s",   libtcc1_a); }

    cmd_run(false, "rm -f %s/usr/bin/tcc",          SYSROOT_DIR);
    cmd_run(false, "rm -rf %s/usr/lib/tcc",         SYSROOT_DIR);
    print_color(COLOR_YELLOW, "[clean] removed %s/usr/bin/tcc and %s/usr/lib/tcc/",
                SYSROOT_DIR, SYSROOT_DIR);

    if (deep) {
        char src_dir[SHORTPATH], tar_path[SHORTPATH];
        path_join2(src_dir,  sizeof(src_dir),  TCC_DIR, TCC_SRC_DIR);
        path_join2(tar_path, sizeof(tar_path), TCC_DIR, TCC_TAR_FNAME);
        if (file_exists(src_dir))  { rm_rf(src_dir);  print_color(COLOR_YELLOW, "[clean] removed %s",  src_dir); }
        if (file_exists(tar_path)) { remove(tar_path); print_color(COLOR_YELLOW, "[clean] removed %s",  tar_path); }
    }
}

void clean_libcervus_build(bool deep) {
    char buf[PATH_MAX];
    for (int i = 0; LCV_UNITS[i].src; i++) {
        path_join2(buf, sizeof(buf), LIBCERVUS_DIR, LCV_UNITS[i].obj);
        if (file_exists(buf)) remove(buf);
    }
    path_join2(buf, sizeof(buf), LIBCERVUS_DIR, "setjmp.o");
    if (file_exists(buf)) remove(buf);
    path_join2(buf, sizeof(buf), LIBCERVUS_DIR, "crt0.o");
    if (file_exists(buf)) remove(buf);
    path_join2(buf, sizeof(buf), LIBCERVUS_DIR, "libcervus.a");
    if (file_exists(buf)) remove(buf);

    cmd_run(false, "rm -f %s/libcervus.a %s/crt0.o", SYSROOT_LIB, SYSROOT_LIB);
    (void)deep;
}

bool build_installer(void) {
    DIR *d = opendir(INSTALLER_DIR);
    if (!d) {
        print_color(COLOR_YELLOW,
            "[installer] directory '%s' not found, skipping", INSTALLER_DIR);
        return true;
    }
    struct dirent *de;
    int built = 0;
    while ((de = readdir(d)) != NULL) {
        const char *nm = de->d_name;
        size_t nlen = strlen(nm);
        if (nlen < 3 || nm[0] == '.') continue;
        if (strcmp(nm + nlen - 2, ".c") != 0) continue;
        app_entry_t e;
        snprintf(e.src,  sizeof(e.src),  "%s/%s",       INSTALLER_DIR, nm);
        snprintf(e.elf,  sizeof(e.elf),  "%s/%.*s.elf", INSTALLER_DIR, (int)(nlen-2), nm);
        snprintf(e.name, sizeof(e.name), "%.*s",        (int)(nlen-2), nm);
        if (!build_one_app(&e)) { closedir(d); return false; }
        built++;
    }
    closedir(d);
    if (built == 0)
        print_color(COLOR_YELLOW, "[installer] no .c sources in '%s'", INSTALLER_DIR);
    return true;
}

bool build_initramfs(void) {
    if (ARG_NO_INITRAMFS) {
        print_color(COLOR_YELLOW, "[initramfs] skipped (--no-initramfs)");
        return true;
    }

    if (!build_limine()) {
        print_color(COLOR_YELLOW, "[initramfs] warning: limine not available yet, boot files will be incomplete");
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
        if (!any_newer) {
            scan_bin_apps();
            for (int i = 0; i < g_nbin; i++) {
                if (file_exists(g_bin_apps[i].elf) &&
                    get_mtime(g_bin_apps[i].elf) > get_mtime(INITRAMFS_TAR)) {
                    any_newer = true; break;
                }
            }
        }
        if (!any_newer && file_exists(SHELL_ELF) &&
            get_mtime(SHELL_ELF) > get_mtime(INITRAMFS_TAR)) {
            any_newer = true;
        }
        if (!any_newer && file_exists(INSTALLER_ELF) &&
            get_mtime(INSTALLER_ELF) > get_mtime(INITRAMFS_TAR)) {
            any_newer = true;
        }
        if (!any_newer && file_exists(SYSROOT_LIB "/libcervus.a") &&
            get_mtime(SYSROOT_LIB "/libcervus.a") > get_mtime(INITRAMFS_TAR)) {
            any_newer = true;
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
        INITRAMFS_ROOTFS "/home",
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
        fprintf(hostname, "cervus");
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
            "\n"
            " Your home directory is /mnt/home (persistent ext2 disk).\n"
            " Files created in /mnt/ will survive reboots.\n"
            " Files outside /mnt/ are in RAM and will be lost on reboot.\n"
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
        print_color(COLOR_RED, "[initramfs] shell.elf not found - boot will drop to nothing!");
        FILE *stub = fopen(INITRAMFS_ROOTFS "/bin/.keep", "w");
        if (stub) fclose(stub);
    }

    scan_bin_apps();
    print_color(COLOR_CYAN, "[initramfs] Copying %d bin program(s) -> rootfs/bin/", g_nbin);
    for (int i = 0; i < g_nbin; i++) {
        if (!file_exists(g_bin_apps[i].elf)) {
            print_color(COLOR_YELLOW, "[initramfs] %s not built, skipping", g_bin_apps[i].src);
            continue;
        }
        char dst[512];
        snprintf(dst, sizeof(dst), "%s/bin/%s", INITRAMFS_ROOTFS, g_bin_apps[i].name);
        if (cmd_run(false, "cp %s %s", g_bin_apps[i].elf, dst) != 0) {
            print_color(COLOR_RED, "[initramfs] Failed to copy %s", g_bin_apps[i].elf);
        } else {
            print_color(COLOR_GREEN, "[initramfs] %s -> rootfs/bin/%s",
                        g_bin_apps[i].elf, g_bin_apps[i].name);
        }
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

    if (file_exists(INSTALLER_ELF)) {
        char dst[512];
        snprintf(dst, sizeof(dst), "%s/bin/install-on-disk", INITRAMFS_ROOTFS);
        if (cmd_run(false, "cp %s %s", INSTALLER_ELF, dst) == 0)
            print_color(COLOR_GREEN, "[initramfs] %s -> rootfs/bin/install-on-disk",
                        INSTALLER_ELF);
        else
            print_color(COLOR_RED, "[initramfs] failed to copy installer");
    }

    if (file_exists(SYSROOT_DIR "/usr")) {
        print_color(COLOR_CYAN, "[initramfs] Copying sysroot -> rootfs/usr...");
        ensure_dir(INITRAMFS_ROOTFS "/usr");
        if (cmd_run(false, "cp -r " SYSROOT_DIR "/usr/. %s/usr/",
                    INITRAMFS_ROOTFS) != 0) {
            print_color(COLOR_RED, "[initramfs] Failed to copy sysroot");
            return false;
        }
        print_color(COLOR_GREEN, "[initramfs] sysroot installed into rootfs/usr/");
    } else {
        print_color(COLOR_YELLOW,
            "[initramfs] " SYSROOT_DIR "/usr not found - skipping sysroot "
            "(did libcervus.a build fail?)");
    }

    FILE *readme = fopen(INITRAMFS_ROOTFS "/home/readme.txt", "w");
    if (readme) {
        fprintf(readme,
            "Cervus OS v0.0.2\n"
            "================\n"
            "\n"
            "This is Cervus - an x86_64 OS written in C.\n"
            "Bootloader: Limine | Filesystem: ext2\n"
            "\n"
            "Built-in shell commands:\n"
            "  help, cd, exit\n"
            "\n"
            "Programs in /bin (always available):\n"
            "  ls, cat, echo, pwd, clear, uname, meminfo, cpuinfo\n"
            "\n"
            "Programs in /apps (run from /apps dir):\n"
            "  hello, calc, cal, date, ps, uptime, find, stat,\n"
            "  hexdump, kill, wc, yes, sleep, ...\n"
            "\n"
            "Persistent storage:\n"
            "  /mnt/home       - user home directory (ext2)\n"
            "  /mnt/usr        - sysroot for libraries and headers\n"
            "  /mnt/tmp        - temporary files (persistent)\n"
            "\n"
            "Source: https://github.com/VeoQeo/Cervus\n"
        );
        fclose(readme);
        print_color(COLOR_GREEN, "[initramfs] created /home/readme.txt");
    }

    FILE *welcome = fopen(INITRAMFS_ROOTFS "/home/welcome.txt", "w");
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
        print_color(COLOR_GREEN, "[initramfs] created /home/welcome.txt");
    }

    ensure_dir(INITRAMFS_ROOTFS "/boot");

    struct { const char *src; const char *dst; bool required; } boot_items[] = {
        { "bin/kernel",                 INITRAMFS_ROOTFS "/boot/kernel",                true  },
        { SHELL_ELF,                    INITRAMFS_ROOTFS "/boot/shell.elf",             true  },
        { "limine/limine-bios.sys",     INITRAMFS_ROOTFS "/boot/limine-bios.sys",       false },
        { "limine/limine-bios-hdd.bin", INITRAMFS_ROOTFS "/boot/limine-bios-hdd.bin",   false },
        { "limine/BOOTX64.EFI",         INITRAMFS_ROOTFS "/boot/BOOTX64.EFI",           false },
        { "limine/BOOTIA32.EFI",        INITRAMFS_ROOTFS "/boot/BOOTIA32.EFI",          false },
        { WALLPAPER_SRC,                INITRAMFS_ROOTFS "/boot/wallpaper.png",         false },
        { NULL, NULL, false }
    };
    for (int i = 0; boot_items[i].src; i++) {
        if (!file_exists(boot_items[i].src)) {
            if (boot_items[i].required) {
                print_color(COLOR_RED, "[initramfs] missing required boot file: %s", boot_items[i].src);
                return false;
            }
            print_color(COLOR_YELLOW, "[initramfs] skip (not built yet): %s", boot_items[i].src);
            continue;
        }
        if (cmd_run(false, "cp %s %s", boot_items[i].src, boot_items[i].dst) != 0) {
            print_color(COLOR_RED, "[initramfs] failed to copy %s", boot_items[i].src);
            if (boot_items[i].required) return false;
        } else {
            print_color(COLOR_GREEN, "[initramfs] %s -> %s",
                        boot_items[i].src, boot_items[i].dst);
        }
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

    if (!build_libcervus()) return false;
    if (!build_tcc())       return false;

    if (!build_all_apps()) return false;
    if (!build_all_bin_apps()) return false;

    if (!build_installer()) return false;

    ensure_dir("bin");
    ensure_dir("obj/kernel");
    ensure_dir("obj/libc");

    const char *base_cflags =
        "-g -O2 -pipe -Wall -Wextra -std=gnu11 -nostdinc -ffreestanding "
        "-fno-stack-protector -fno-stack-check -fno-lto -fno-PIC "
        "-ffunction-sections -fdata-sections "
        "-m64 -march=x86-64 -mabi=sysv -mcmodel=kernel "
        "-mno-red-zone -mgeneral-regs-only -fcf-protection=none";

    const char *sse_base_cflags =
        "-g -O2 -pipe -Wall -Wextra -std=gnu11 -nostdinc -ffreestanding "
        "-fno-stack-protector -fno-stack-check -fno-lto -fno-PIC "
        "-ffunction-sections -fdata-sections "
        "-m64 -march=x86-64 -mabi=sysv -mcmodel=kernel "
        "-mno-red-zone -fcf-protection=none";

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

        if (file_exists(obj_path) && get_mtime(src) <= get_mtime(obj_path)
            && get_mtime("builder/build.c") <= get_mtime(obj_path)) continue;

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
            snprintf(flags, sizeof(flags), "%s %s",
                     sse ? sse_base_cflags : base_cflags,
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
        print_color(COLOR_RED, "[module 0] shell.elf not found - boot will fail!");
    }

    bool has_initramfs = !ARG_NO_INITRAMFS && file_exists(INITRAMFS_TAR);
    if (has_initramfs) {
        cmd_run(false, "cp %s iso_root/boot/initramfs.tar", INITRAMFS_TAR);
        print_color(COLOR_GREEN, "[module 1] initramfs.tar -> iso_root/boot/initramfs.tar");
    } else {
        print_color(COLOR_YELLOW, "[module 1] initramfs.tar not found - booting without rootfs");
    }

    FILE *f = fopen("limine.conf", "w");
    if (!f) { print_color(COLOR_RED, "Cannot create limine.conf"); return false; }

    if (file_exists(WALLPAPER_SRC))
        fprintf(f, "wallpaper: %s\n", WALLPAPER_DST);

    fprintf(f, "timeout: 5\n\n");

    fprintf(f,
        "/%s %s (Install / Live)\n"
        "    protocol: limine\n"
        "    path: boot():/boot/kernel\n",
        IMAGE_NAME, VERSION);

    if (has_elf) {
        fprintf(f,
            "    module_path: boot():/boot/shell.elf\n"
            "    module_cmdline: init\n");
    }
    if (has_initramfs) {
        fprintf(f,
            "    module_path: boot():/boot/initramfs.tar\n"
            "    module_cmdline: initramfs\n");
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
    (void)!symlink(strrchr(iso_name, '/') + 1, link);

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
    char confirm[10];
    if (scanf("%9s", confirm) != 1) { printf("Aborted.\n"); return; }
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

        for (int j = 0; j < level; j++) fprintf(out, "|   ");

        if (S_ISDIR(st.st_mode)) {
            fprintf(out, "+-- %s/\n", name);
            generate_tree_recursive(path, out, level + 1);
        } else {
            fprintf(out, "+-- %s (%ld bytes)\n", name, st.st_size);
            if (!ARG_STRUCTURE_ONLY && should_print_content(name)) {
                FILE *src = fopen(path, "r");
                if (src) {
                    char line[4096]; int ln = 1;
                    while (fgets(line, sizeof(line), src) && ln <= 5000) {
                        for (int j = 0; j <= level; j++) fprintf(out, "|   ");
                        fprintf(out, "| %4d: %s", ln++, line);
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

static bool limine_conf_has_cervus(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return false;
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, CERVUS_MARKER)) { fclose(f); return true; }
    }
    fclose(f);
    return false;
}

static bool __attribute__((unused)) copy_file_raw(const char *src, const char *dst) {
    FILE *in = fopen(src, "r");
    if (!in) return false;
    FILE *out = fopen(dst, "w");
    if (!out) { fclose(in); return false; }
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
        fwrite(buf, 1, n, out);
    fclose(in);
    fclose(out);
    return true;
}

static bool ask_yes_no(const char *question) {
    printf("%s%s [y/n]: %s", COLOR_YELLOW, question, COLOR_RESET);
    fflush(stdout);
    char buf[16];
    if (!fgets(buf, sizeof(buf), stdin)) return false;
    return (buf[0] == 'y' || buf[0] == 'Y');
}

void reset_hardware_conf(void) {
    check_sudo();

    if (!file_exists(LIMINE_CONF_BACKUP)) {
        print_color(COLOR_RED, "No backup found at %s", LIMINE_CONF_BACKUP);
        print_color(COLOR_RED, "Nothing to restore. Was hardwaretest ever run?");
        return;
    }

    print_color(COLOR_CYAN, "Restoring original Limine configuration...");

    if (cmd_run(true, "cp %s %s", LIMINE_CONF_BACKUP, LIMINE_CONF_PATH) != 0) {
        print_color(COLOR_RED, "Failed to restore %s", LIMINE_CONF_PATH);
        return;
    }

    print_color(COLOR_GREEN, "Original limine.conf restored from backup");

    if (file_exists(CERVUS_BOOT_DIR)) {
        print_color(COLOR_CYAN, "Removing %s ...", CERVUS_BOOT_DIR);
        cmd_run(true, "rm -rf %s", CERVUS_BOOT_DIR);
    }

    print_color(COLOR_GREEN, "Hardware configuration reset complete");
    print_color(COLOR_CYAN, "--- Current limine.conf ---");
    cmd_run(false, "cat %s", LIMINE_CONF_PATH);
    print_color(COLOR_CYAN, "---------------------------");

    if (ask_yes_no("Reboot now?")) {
        print_color(COLOR_YELLOW, "Rebooting...");
        cmd_run(false, "reboot");
    }
}

static void write_cervus_entries(FILE *f, bool has_elf, bool has_initramfs) {
    if (file_exists(WALLPAPER_SRC))
        fprintf(f, "wallpaper: boot():/cervus/wallpaper.png\n");

    fprintf(f,
        "/%s %s (Install / Live)\n"
        "    protocol: limine\n"
        "    path: boot():/cervus/kernel\n",
        IMAGE_NAME, VERSION);
    if (has_elf) {
        fprintf(f,
            "    module_path: boot():/cervus/shell.elf\n"
            "    module_cmdline: init\n");
    }
    if (has_initramfs) {
        fprintf(f,
            "    module_path: boot():/cervus/initramfs.tar\n"
            "    module_cmdline: initramfs\n");
    }

    fprintf(f,
        "/%s %s (Installed - boot from disk)\n"
        "    protocol: limine\n"
        "    path: boot():/cervus/kernel\n",
        IMAGE_NAME, VERSION);
    if (has_elf) {
        fprintf(f,
            "    module_path: boot():/cervus/shell.elf\n"
            "    module_cmdline: init\n");
    }
}

void hardware_test(void) {
    check_sudo();

    if (!file_exists(LIMINE_CONF_PATH)) {
        print_color(COLOR_RED, "Limine config not found at %s", LIMINE_CONF_PATH);
        print_color(COLOR_RED, "Is Limine installed as your bootloader?");
        print_color(COLOR_YELLOW, "Expected path: %s", LIMINE_CONF_PATH);
        print_color(COLOR_YELLOW, "Install Limine first: https://github.com/limine-bootloader/limine");
        return;
    }

    print_color(COLOR_GREEN, "=== Cervus Hardware Test Setup ===");
    print_color(COLOR_CYAN, "Limine config found: %s", LIMINE_CONF_PATH);

    print_color(COLOR_CYAN, "Step 1: Building Cervus...");

    if (!compile_kernel()) {
        print_color(COLOR_RED, "Kernel compilation failed");
        return;
    }

    if (!build_initramfs()) {
        print_color(COLOR_RED, "initramfs build failed");
        return;
    }

    if (!file_exists("bin/kernel")) {
        print_color(COLOR_RED, "bin/kernel not found after build!");
        return;
    }

    print_color(COLOR_CYAN, "Step 2: Checking Limine configuration...");

    bool already_has_cervus = limine_conf_has_cervus(LIMINE_CONF_PATH);

    if (already_has_cervus) {
        print_color(COLOR_GREEN, "Cervus entry already present in limine.conf");
        print_color(COLOR_CYAN, "Updating boot files only...");
    } else {
        print_color(COLOR_CYAN, "No Cervus entry found, will add one");

        if (!file_exists(LIMINE_CONF_BACKUP)) {
            print_color(COLOR_YELLOW, "Step 2a: Backing up original limine.conf...");
            if (cmd_run(true, "cp %s %s", LIMINE_CONF_PATH, LIMINE_CONF_BACKUP) != 0) {
                print_color(COLOR_RED, "Failed to backup limine.conf!");
                return;
            }
            print_color(COLOR_GREEN, "Backup saved: %s", LIMINE_CONF_BACKUP);
            print_color(COLOR_YELLOW, "To restore later: ./build --resethardwareconf");
        } else {
            print_color(COLOR_GREEN, "Backup already exists: %s", LIMINE_CONF_BACKUP);
        }
    }

    print_color(COLOR_CYAN, "Step 3: Copying Cervus files to /boot...");

    cmd_run(true, "mkdir -p %s", CERVUS_BOOT_DIR);

    if (cmd_run(true, "cp bin/kernel %s/kernel", CERVUS_BOOT_DIR) != 0) {
        print_color(COLOR_RED, "Failed to copy kernel");
        return;
    }
    print_color(COLOR_GREEN, "  kernel -> %s/kernel", CERVUS_BOOT_DIR);

    bool has_elf = file_exists(SHELL_ELF);
    if (has_elf) {
        cmd_run(true, "cp %s %s/shell.elf", SHELL_ELF, CERVUS_BOOT_DIR);
        print_color(COLOR_GREEN, "  shell.elf -> %s/shell.elf", CERVUS_BOOT_DIR);
    }

    bool has_initramfs = file_exists(INITRAMFS_TAR);
    if (has_initramfs) {
        cmd_run(true, "cp %s %s/initramfs.tar", INITRAMFS_TAR, CERVUS_BOOT_DIR);
        print_color(COLOR_GREEN, "  initramfs.tar -> %s/initramfs.tar", CERVUS_BOOT_DIR);
    }

    if (file_exists(WALLPAPER_SRC)) {
        cmd_run(true, "cp %s %s/wallpaper.png", WALLPAPER_SRC, CERVUS_BOOT_DIR);
        print_color(COLOR_GREEN, "  wallpaper -> %s/wallpaper.png", CERVUS_BOOT_DIR);
    }

    if (!already_has_cervus) {
        print_color(COLOR_CYAN, "Step 4: Adding Cervus entries to limine.conf...");

        FILE *f = fopen(LIMINE_CONF_PATH, "a");
        if (!f) {
            print_color(COLOR_RED, "Cannot open %s for writing", LIMINE_CONF_PATH);
            return;
        }

        fprintf(f, "\n%s\n", CERVUS_MARKER);
        write_cervus_entries(f, has_elf, has_initramfs);
        fprintf(f, "%s\n", "# --- END CERVUS ---");

        fclose(f);
        print_color(COLOR_GREEN, "Cervus boot entries added to limine.conf");
    } else {
        print_color(COLOR_CYAN, "Step 4: Updating Cervus entries in limine.conf...");

        FILE *orig = fopen(LIMINE_CONF_PATH, "r");
        if (!orig) {
            print_color(COLOR_RED, "Cannot read %s", LIMINE_CONF_PATH);
            return;
        }

        char tmppath[PATH_MAX];
        snprintf(tmppath, sizeof(tmppath), "%s.tmp", LIMINE_CONF_PATH);
        FILE *tmp = fopen(tmppath, "w");
        if (!tmp) { fclose(orig); print_color(COLOR_RED, "Cannot create temp file"); return; }

        char line[1024];
        bool in_cervus = false;
        while (fgets(line, sizeof(line), orig)) {
            if (strstr(line, CERVUS_MARKER)) {
                in_cervus = true;
                continue;
            }
            if (in_cervus && strstr(line, "# --- END CERVUS ---")) {
                in_cervus = false;
                continue;
            }
            if (!in_cervus) {
                fputs(line, tmp);
            }
        }
        fclose(orig);

        fprintf(tmp, "\n%s\n", CERVUS_MARKER);
        write_cervus_entries(tmp, has_elf, has_initramfs);
        fprintf(tmp, "%s\n", "# --- END CERVUS ---");
        fclose(tmp);

        cmd_run(false, "mv %s %s", tmppath, LIMINE_CONF_PATH);
        print_color(COLOR_GREEN, "Cervus entries updated in limine.conf");
    }

    print_color(COLOR_GREEN, "\n=== Setup Complete ===");
    print_color(COLOR_CYAN, "--- Current limine.conf ---");
    cmd_run(false, "cat %s", LIMINE_CONF_PATH);
    print_color(COLOR_CYAN, "---------------------------");

    print_color(COLOR_GREEN, "\nCervus will appear in your Limine boot menu.");
    print_color(COLOR_YELLOW, "First boot:    select '%s %s (Install / Live)'", IMAGE_NAME, VERSION);
    print_color(COLOR_YELLOW, "After install: select '%s %s (Installed - boot from disk)'", IMAGE_NAME, VERSION);
    print_color(COLOR_YELLOW, "To restore original config: sudo ./build --resethardwareconf");

    if (ask_yes_no("\nReboot now to test on real hardware?")) {
        print_color(COLOR_YELLOW, "Rebooting...");
        cmd_run(false, "reboot");
    } else {
        print_color(COLOR_GREEN, "OK. Reboot manually when ready.");
    }
}

static const char* find_ovmf(void) {
    const char *ovmf_paths[] = {
        "/usr/share/edk2/x64/OVMF.4m.fd",
        "/usr/share/edk2/x64/OVMF_CODE.4m.fd",
        "/usr/share/edk2/ovmf/OVMF.fd",
        "/usr/share/edk2/ovmf/OVMF_CODE.fd",
        "/usr/share/ovmf/x64/OVMF.fd",
        "/usr/share/ovmf/x64/OVMF_CODE.fd",
        "/usr/share/ovmf/OVMF.fd",
        "/usr/share/OVMF/OVMF.fd",
        "/usr/share/OVMF/OVMF_CODE.fd",
        "/usr/share/qemu/OVMF.fd",
        NULL
    };
    for (int i = 0; ovmf_paths[i]; i++) {
        if (file_exists(ovmf_paths[i])) return ovmf_paths[i];
    }
    return NULL;
}

static void print_help(void) {
    printf("%sCervus OS build system%s\n\n", COLOR_BOLD, COLOR_RESET);
    printf("Usage: ./build [command] [options]\n\n");
    printf("Commands:\n");
    printf("  run              Build kernel + initramfs + ISO, then launch QEMU (BIOS, with disk)\n");
    printf("  run-uefi         Build kernel + initramfs + ISO, then launch QEMU (UEFI, with disk)\n");
    printf("  run-installed    Launch QEMU from cervus_disk.img only (BIOS, no ISO, real hardware sim)\n");
    printf("  run-installed-uefi Launch QEMU from cervus_disk.img only (UEFI, no ISO)\n");
    printf("  run-fresh        Wipe disk and run installer from ISO again (BIOS)\n");
    printf("  run-fresh-uefi   Wipe disk and run installer from ISO again (UEFI)\n");
    printf("  hardwaretest     Build & install Cervus into Limine boot menu (requires sudo)\n");
    printf("  flash            Flash latest ISO to USB device (requires sudo)\n");
    printf("  clean            Remove all build artifacts\n");
    printf("  cleaniso         Remove only ISO images in demo_iso/\n");
    printf("  gitclean         Same as clean (for git commit prep)\n");
    printf("  help             Show this message\n\n");
    printf("Subcomponent build (debug):\n");
    printf("  _libcervus       Build only libcervus.a + crt0.o into sysroot\n");
    printf("  _tcc             Build only TCC (and libcervus first) into sysroot\n\n");
    printf("Options:\n");
    printf("  --tree [files]         Generate OS-TREE.txt (optional: only list files)\n");
    printf("  --structure-only       Generate tree without file contents\n");
    printf("  --no-clean             Keep obj/ and bin/ after run\n");
    printf("  --no-initramfs         Skip initramfs.tar creation\n");
    printf("  --reset-disk           Re-create empty disk image\n");
    printf("  --live                 Run without disk (Live Mode, BIOS only)\n");
    printf("  --resethardwareconf    Restore original Limine config (requires sudo)\n\n");
    printf("Boot entries in Limine menu:\n");
    printf("  Install / Live                  - first boot, runs installer, uses initramfs\n");
    printf("  Installed - boot from disk      - after install, no initramfs, uses ext2 disk\n\n");
    printf("Hardware test workflow:\n");
    printf("  sudo ./build hardwaretest        # build, install to /boot, add boot entries\n");
    printf("  sudo ./build --resethardwareconf # restore original bootloader config\n\n");
    printf("UEFI Requirements:\n");
    printf("  Install OVMF: sudo apt install ovmf (Debian/Ubuntu) or equivalent\n\n");
    printf("Examples:\n");
    printf("  ./build run                      # normal boot with disk (BIOS)\n");
    printf("  ./build run-uefi                 # normal boot with disk (UEFI)\n");
    printf("  ./build run --live               # boot without disk (Live Mode, BIOS)\n");
    printf("  ./build run-fresh-uefi           # fresh disk, boot installer (UEFI)\n");
    printf("  ./build run --reset-disk         # fresh disk (BIOS)\n");
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
        } else if (strcmp(argv[i], "--resethardwareconf") == 0) {
            ARG_RESET_HW_CONF = true;
        } else if (strcmp(argv[i], "--reset-disk") == 0) {
            ARG_RESET_DISK = true;
        } else if (strcmp(argv[i], "--live") == 0) {
            ARG_LIVE = true;
        } else if (argv[i][0] != '-') {
            command = argv[i];
        }
    }

    if (ARG_TREE && !command) { do_generate_tree(); return 0; }

    if (ARG_RESET_HW_CONF) {
        reset_hardware_conf();
        return 0;
    }

    if (!command || strcmp(command, "help") == 0) {
        print_help();
        return 0;
    }

    if (strcmp(command, "clean") == 0 || strcmp(command, "gitclean") == 0) {
        for (int i = 0; DIRS_TO_CLEAN[i];  i++) rm_rf(DIRS_TO_CLEAN[i]);
        clean_apps_elfs();
        clean_bin_elfs();
        for (int i = 0; FILES_TO_CLEAN[i]; i++)
            if (file_exists(FILES_TO_CLEAN[i])) remove(FILES_TO_CLEAN[i]);
        clean_libcervus_build(true);
        clean_tcc_build(true);
        cmd_run(false, "rm -f " INSTALLER_DIR "/*.elf 2>/dev/null");
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

    if (strcmp(command, "hardwaretest") == 0) {
        hardware_test();
        return 0;
    }

    if (strcmp(command, "_libcervus") == 0) {
        return build_libcervus() ? 0 : 1;
    }
    if (strcmp(command, "_tcc") == 0) {
        if (!build_libcervus()) return 1;
        return build_tcc() ? 0 : 1;
    }

    if (strcmp(command, "run-installed-uefi") == 0) {
        if (!file_exists("cervus_disk.img")) {
            print_color(COLOR_RED, "cervus_disk.img not found. Run './build run' first.");
            return 1;
        }
        const char *ovmf = find_ovmf();
        if (!ovmf) {
            print_color(COLOR_RED, "OVMF not found. Install: sudo apt install ovmf");
            return 1;
        }
        print_color(COLOR_GREEN, "Starting QEMU with UEFI firmware from disk only...");
        print_color(COLOR_GREEN, "Using OVMF: %s", ovmf);
        cmd_run(false,
            "GDK_BACKEND=x11 qemu-system-x86_64 -machine pc"
            " -bios %s"
            " -drive file=cervus_disk.img,format=raw,if=ide"
            " -serial stdio"
            " -m 8G -smp 8 -cpu qemu64,+fsgsbase -display gtk,grab-on-hover=on"
            " 2>&1 | tee log.txt",
            ovmf);
        return 0;
    }

    if (strcmp(command, "run-installed") == 0) {
        if (!file_exists("cervus_disk.img")) {
            print_color(COLOR_RED, "cervus_disk.img not found. "
                                   "Run './build run' first to build and install Cervus.");
            return 1;
        }
        print_color(COLOR_CYAN, "Starting QEMU from disk only (no ISO, simulates real hardware)...");
        cmd_run(false,
            "GDK_BACKEND=x11 qemu-system-x86_64 -machine pc"
            " -drive file=cervus_disk.img,format=raw,if=ide"
            " -serial stdio"
            " -m 8G -smp 8 -cpu qemu64,+fsgsbase -display gtk,grab-on-hover=on"
            " 2>&1 | tee log.txt");
        return 0;
    }

    if (strcmp(command, "run-fresh") == 0) {
        ARG_RESET_DISK = true;
        command = "run";
    }

    if (strcmp(command, "run-fresh-uefi") == 0) {
        ARG_RESET_DISK = true;
        command = "run-uefi";
    }

    if (strcmp(command, "run") == 0 || strcmp(command, "run-uefi") == 0) {
        bool use_uefi = (strcmp(command, "run-uefi") == 0);

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

        if (use_uefi) {
            const char *ovmf = find_ovmf();
            if (!ovmf) {
                print_color(COLOR_RED, "OVMF not found. Install: sudo apt install ovmf");
                return 1;
            }
            print_color(COLOR_GREEN, "Using OVMF: %s", ovmf);
        }

        if (ARG_LIVE && !use_uefi) {
            print_color(COLOR_CYAN, "Starting QEMU in Live Mode (no disk, BIOS)...");
            cmd_run(false,
                "GDK_BACKEND=x11 qemu-system-x86_64 -machine pc"
                " -cdrom %s -boot d"
                " -serial stdio"
                " -m 8G -smp 8 -cpu qemu64,+fsgsbase -display gtk,grab-on-hover=on"
                " 2>&1 | tee log.txt",
                iso_path);
        } else if (ARG_LIVE && use_uefi) {
            print_color(COLOR_RED, "Live mode (--live) is not supported in UEFI mode yet");
            return 1;
        } else {
            if (!file_exists("cervus_disk.img") || ARG_RESET_DISK) {
                if (ARG_RESET_DISK && file_exists("cervus_disk.img")) {
                    print_color(COLOR_YELLOW, "[disk] --reset-disk: removing old disk image");
                    remove("cervus_disk.img");
                }
                print_color(COLOR_CYAN, "Creating empty disk image (256MB)...");
                cmd_run(false, "dd if=/dev/zero of=cervus_disk.img bs=1M count=256 2>/dev/null");
                print_color(COLOR_GREEN, "Disk created (OS installer will format on first boot)");
            } else {
                print_color(COLOR_GREEN, "Using existing cervus_disk.img (persistent data)");
            }

            if (use_uefi) {
                const char *ovmf = find_ovmf();
                print_color(COLOR_GREEN, "Starting QEMU with UEFI firmware (IDE mode)...");
                cmd_run(false,
                    "GDK_BACKEND=x11 qemu-system-x86_64 -machine pc"
                    " -bios %s"
                    " -cdrom %s -boot d"
                    " -serial stdio"
                    " -m 8G -smp 8 -cpu qemu64,+fsgsbase -display gtk,grab-on-hover=on"
                    " -drive file=cervus_disk.img,format=raw,if=ide,index=0,media=disk"
                    " 2>&1 | tee log.txt",
                    ovmf, iso_path);
            } else {
                print_color(COLOR_GREEN, "Starting QEMU with BIOS...");
                cmd_run(false,
                    "GDK_BACKEND=x11 qemu-system-x86_64 -machine pc"
                    " -cdrom %s -boot d"
                    " -serial stdio"
                    " %s"
                    " 2>&1 | tee log.txt",
                    iso_path, QEMUFLAGS);
            }
        }

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
            clean_bin_elfs();
            clean_libcervus_build(false);
            clean_tcc_build(false);
            print_color(COLOR_GREEN, "[post-run] Done.");
        }
        return 0;
    }

    print_color(COLOR_RED, "Unknown command: %s (try: ./build help)", command);
    return 1;
}