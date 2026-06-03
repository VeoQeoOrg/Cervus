#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/cervus.h>
#include <sys/syscall.h>
#include <stddef.h>

#define NVT 12
#define C_RESET  "\x1b[0m"
#define C_RED    "\x1b[1;31m"
#define C_GREEN  "\x1b[1;32m"
#define C_YELLOW "\x1b[1;33m"

static int vt_pid[NVT];
static char g_shell[256] = "/bin/csh";

extern char **environ;

static int sys_disk_mount(const char *dev, const char *path) {
    return (int)syscall2(SYS_DISK_MOUNT, dev, path);
}

static void term_cooked(void) {
    struct termios t;
    if (tcgetattr(0, &t) < 0) return;
    t.c_lflag |= (ICANON | ECHO | ISIG);
    tcsetattr(0, TCSANOW, &t);
}

static int launch_installer_boot(void) {
    const char *path = "/bin/cervus-installer";
    struct stat st;
    if (stat(path, &st) != 0) return -1;

    const char *argv[2]  = { path, NULL };
    const char *envp[3]  = { "MODE=live", "BOOT_PROMPT=1", NULL };

    pid_t child = fork();
    if (child < 0) return -1;
    if (child == 0) {
        chdir("/");
        execve(path, (char *const *)argv, (char *const *)envp);
        _exit(127);
    }
    int status = 0;
    waitpid(child, &status, 0);
    fputs("\x1b[?25h\x1b[2J\x1b[H", stdout);
    fflush(stdout);
    return (status >> 8) & 0xFF;
}

static void read_default_shell(void) {
    int fd = open("/mnt/etc/shell", O_RDONLY, 0);
    if (fd < 0) fd = open("/etc/shell", O_RDONLY, 0);
    if (fd < 0) return;
    char buf[256];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return;
    int i = 0;
    while (i < (int)n && buf[i] != '\n' && buf[i] != '\r' && buf[i] != ' ') i++;
    buf[i] = '\0';
    if (buf[0] == '/') {
        struct stat st;
        if (stat(buf, &st) == 0 && st.st_type != 1) {
            strncpy(g_shell, buf, sizeof(g_shell) - 1);
            g_shell[sizeof(g_shell) - 1] = '\0';
        }
    }
}

static void boot_setup(void) {
    static const char *const DISK_PREFIXES[] = { "nvme0n1", "sda", "hda" };

    int rooted_on_disk = 0;
    {
        cervus_mount_info_t mnts[16];
        long nm = cervus_list_mounts(mnts, 16);
        for (long i = 0; i < nm; i++) {
            if (strcmp(mnts[i].path, "/") != 0) continue;
            if (strcmp(mnts[i].fstype, "ramfs")     == 0) break;
            if (strcmp(mnts[i].fstype, "initramfs") == 0) break;
            if (strcmp(mnts[i].fstype, "devfs")     == 0) break;
            rooted_on_disk = 1;
            break;
        }
    }

    const char *disk_pfx = NULL;
    char dev_path[32], dev_path2[32];
    struct stat dev_st;
    int has_disk = 0;
    for (size_t k = 0; k < sizeof(DISK_PREFIXES) / sizeof(DISK_PREFIXES[0]); k++) {
        const char *pfx = DISK_PREFIXES[k];
        size_t pl = strlen(pfx);
        const char *sep = (pl > 0 && pfx[pl - 1] >= '0' && pfx[pl - 1] <= '9') ? "p" : "";
        snprintf(dev_path,  sizeof(dev_path),  "/dev/%s",    pfx);
        snprintf(dev_path2, sizeof(dev_path2), "/dev/%s%s2", pfx, sep);
        int dh  = (stat(dev_path,  &dev_st) == 0);
        int dh2 = (stat(dev_path2, &dev_st) == 0);
        if (dh2) { disk_pfx = pfx; has_disk = dh; break; }
        if (dh && !disk_pfx) { disk_pfx = pfx; has_disk = 1; }
    }
    if (!disk_pfx) disk_pfx = "hda";

    char part2_name[16];
    {
        size_t pl = strlen(disk_pfx);
        const char *sep = (pl > 0 && disk_pfx[pl - 1] >= '0' && disk_pfx[pl - 1] <= '9') ? "p" : "";
        snprintf(part2_name, sizeof(part2_name), "%s%s2", disk_pfx, sep);
    }

    int disk_mounted = 0;
    if (!rooted_on_disk && has_disk) {
        int code = launch_installer_boot();
        if (code == 10) {
            if (sys_disk_mount(part2_name, "/mnt") == 0) disk_mounted = 1;
        }
    }

    if (rooted_on_disk) {
        setenv("HOME", "/home", 1);
        setenv("PATH", "/bin:/apps:/usr/bin", 1);
        setenv("MODE", "installed", 1);
    } else if (disk_mounted) {
        setenv("HOME", "/mnt/home", 1);
        setenv("PATH", "/mnt/bin:/mnt/apps:/mnt/usr/bin", 1);
        setenv("MODE", "installed", 1);
    } else {
        setenv("HOME", "/", 1);
        setenv("PATH", "/bin:/apps:/usr/bin", 1);
        setenv("MODE", "live", 1);
    }

    read_default_shell();
    setenv("SHELL", g_shell, 1);
    setenv("TERM", "cervus-vt", 1);
}

static void spawn_shell(int vt) {
    int pid = fork();
    if (pid < 0) {
        cervus_vt_clear_shell(vt);
        return;
    }
    if (pid == 0) {
        cervus_vt_set_ctty(vt);
        close(0);
        close(1);
        close(2);
        open("/dev/tty", O_RDONLY, 0);
        open("/dev/tty", O_WRONLY, 0);
        open("/dev/tty", O_WRONLY, 0);
        char *argv[] = { g_shell, NULL };
        execve(g_shell, argv, environ);
        _exit(127);
    }
    vt_pid[vt] = pid;
}

int main(void) {
    for (int i = 0; i < NVT; i++) vt_pid[i] = 0;

    boot_setup();
    term_cooked();

    spawn_shell(0);

    for (;;) {
        int status;
        int d;
        while ((d = waitpid(-1, &status, WNOHANG)) > 0) {
            for (int i = 0; i < NVT; i++) {
                if (vt_pid[i] == d) {
                    vt_pid[i] = 0;
                    if (i == 0) spawn_shell(0);
                    else        cervus_vt_clear_shell(i);
                    break;
                }
            }
        }

        int n = cervus_vt_spawn_poll();
        if (n >= 0 && n < NVT && vt_pid[n] == 0) {
            spawn_shell(n);
            continue;
        }

        usleep(16000);
    }
    return 0;
}
