#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/wait.h>
#include <sys/syscall.h>

#define PIECE_TEXT   0
#define PIECE_RODATA 1
#define PIECE_DATA   2
#define PIECE_BSS    3
#define PIECE_HEAP   4
#define PIECE_STACK  5

static const char *KINDS[] = { "text","rodata","data","bss","heap","stack" };
static const char g_marker[] = "GUARDIAN-PROTECTED-STRING-DO-NOT-EDIT-1234";

static int kind_from_name(const char *s) {
    for (int i = 0; i < 6; i++) if (!strcmp(s, KINDS[i])) return i;
    return -1;
}

static int cat_puzzle(int pid) {
    char path[40];
    snprintf(path, sizeof path, "/proc/%d/puzzle", pid);
    int fd = open(path, O_RDONLY);
    if (fd < 0) { printf("  cannot open %s\n", path); return -1; }
    char buf[1600];
    int n = (int)read(fd, buf, sizeof buf - 1);
    close(fd);
    if (n <= 0) return -1;
    buf[n] = 0;
    fputs(buf, stdout);
    return 0;
}

static void usage(void) {
    printf("splinter -- puzzle-process laboratory\n");
    printf("  splinter run <program> [args...]   launch a program, show how the kernel splits it\n");
    printf("  splinter map [pid]                 show the piece map + stats of a process (default: self)\n");
    printf("  splinter ps                        list every puzzle process and its piece stats\n");
    printf("  splinter eat <text|rodata|data|bss|heap|stack>\n");
    printf("                                     eat one of my own pieces, watch it regenerate\n");
    printf("  splinter guard                     tamper my own code, watch the guardian heal it\n");
    printf("  splinter spec                      run the speculative commit/abort engine self-test\n");
}

static int cmd_spec(void) {
    printf("splinter/spec: launching speculative workers on free cores...\n");
    printf("  (a worker mutates a private shadow of a memory region; the kernel then\n");
    printf("   commits the delta, aborts it, or refuses on a write-write conflict)\n\n");
    long score = syscall2(SYS_SPEC, 0, 0);
    if (score < 0) { printf("splinter/spec: engine error %ld\n", score); return 1; }
    struct { int bit; const char *name; } t[] = {
        {  1, "commit            (worker delta merged into master)" },
        {  2, "abort             (speculative delta discarded)" },
        {  4, "conflict WAW      (both wrote same page -> refused)" },
        {  8, "parallel disjoint (worker + main different pages, both kept)" },
        { 16, "conflict RAW      (main wrote a page the worker READ -> refused)" },
        { 32, "non-blocking      (parent ran concurrently, then committed)" },
        { 64, "parallel-for      (auto-split work across cores, all committed)" },
    };
    for (int i = 0; i < 7; i++)
        printf("  [%s] %s\n", (score & t[i].bit) ? "PASS" : "FAIL", t[i].name);
    printf("\nsplinter/spec: %s (see serial log for the [spec] trace)\n",
           score == 0x7F ? "all speculation primitives work" : "some primitives failed");
    return score == 0x7F ? 0 : 1;
}

static int cmd_run(int argc, char **argv) {
    if (argc < 3) { printf("usage: splinter run <program> [args...]\n"); return 1; }
    char **cargv = &argv[2];

    pid_t pid = fork();
    if (pid < 0) { printf("splinter: fork failed\n"); return 1; }
    if (pid == 0) {
        execvp(cargv[0], cargv);
        printf("splinter: cannot exec '%s'\n", cargv[0]);
        _exit(127);
    }

    printf("splinter: launched '%s' as pid %d -- how the kernel split it into pieces:\n\n",
           cargv[0], (int)pid);
    for (int i = 0; i < 60; i++) {
        char path[40];
        snprintf(path, sizeof path, "/proc/%d/puzzle", (int)pid);
        int fd = open(path, O_RDONLY);
        if (fd >= 0) {
            char buf[1600];
            int n = (int)read(fd, buf, sizeof buf - 1);
            close(fd);
            if (n > 0) {
                buf[n] = 0;
                if (!strstr(buf, "no puzzle")) { fputs(buf, stdout); break; }
            }
        }
        syscall1(SYS_SLEEP_NS, 4000000ULL);
    }

    int status = 0;
    waitpid(pid, &status, 0);
    printf("\nsplinter: '%s' exited (status %d)\n", cargv[0], status);
    return 0;
}

static int cmd_map(int argc, char **argv) {
    int pid = (argc >= 3) ? atoi(argv[2]) : (int)getpid();
    return cat_puzzle(pid);
}

static int is_num(const char *s) {
    if (!*s) return 0;
    for (; *s; s++) if (*s < '0' || *s > '9') return 0;
    return 1;
}

static int cmd_ps(void) {
    DIR *d = opendir("/proc");
    if (!d) { printf("splinter: cannot open /proc\n"); return 1; }
    printf("splinter ps -- puzzle processes:\n");
    struct dirent *e;
    int count = 0;
    while ((e = readdir(d)) != NULL) {
        if (!is_num(e->d_name)) continue;
        char path[48];
        snprintf(path, sizeof path, "/proc/%s/puzzle", e->d_name);
        int fd = open(path, O_RDONLY);
        if (fd < 0) continue;
        char buf[1600];
        int n = (int)read(fd, buf, sizeof buf - 1);
        close(fd);
        if (n <= 0) continue;
        buf[n] = 0;
        if (strstr(buf, "no puzzle")) continue;

        char *nl = strchr(buf, '\n');
        if (nl) *nl = 0;
        printf("  %s\n", buf);
        if (nl) {
            char *rg = strstr(nl + 1, "regens=");
            if (rg) {
                char *rgnl = strchr(rg, '\n');
                if (rgnl) *rgnl = 0;
                printf("      %s\n", rg);
            }
        }
        count++;
    }
    closedir(d);
    printf("  (%d puzzle process(es))\n", count);
    return 0;
}

static int cmd_eat(const char *kindname) {
    int kind = kind_from_name(kindname);
    if (kind < 0) { printf("splinter: unknown piece '%s'\n", kindname); return 1; }

    long before = syscall2(SYS_PUZZLE, 1, 0);
    printf("splinter: eating my '%s' piece (kernel unmaps every page of it)...\n", KINDS[kind]);
    long unmapped = syscall2(SYS_PUZZLE, 0, (uint64_t)kind);
    long regens = syscall2(SYS_PUZZLE, 1, 0);
    long alive  = syscall2(SYS_PUZZLE, 3, 0);
    printf("  survived: %ld page(s) eaten, %ld regenerated on the fly from the ELF\n",
           unmapped, regens - before);
    printf("  alive_pieces=%ld\n", alive);
    return 0;
}

static void read_marker(char *out, int n) {
    const volatile char *m = g_marker;
    for (int i = 0; i < n; i++) out[i] = (char)m[i];
    out[n] = 0;
}

static int cmd_guard(void) {
    char buf[48];
    read_marker(buf, 41);
    printf("splinter/guard: RODATA marker = \"%s\"\n", buf);

    long before = syscall2(SYS_PUZZLE, 5, 0);
    printf("  tampering my own RODATA (writing 0xEE over 16 bytes)...\n");
    long r = syscall3(SYS_PUZZLE, 6, (uint64_t)(uintptr_t)g_marker, 16);
    if (r < 0) { printf("  tamper rejected (%ld)\n", r); return 1; }

    read_marker(buf, 41);
    printf("  right after tamper  = \"%s\"\n", buf);
    printf("  waiting for the guardian (running on another core)...\n");
    syscall1(SYS_SLEEP_NS, 700000000ULL);

    read_marker(buf, 41);
    long after = syscall2(SYS_PUZZLE, 5, 0);
    printf("  after guardian      = \"%s\"\n", buf);
    printf("  guardian restores: %ld -> %ld\n", before, after);
    printf("splinter/guard: %s\n",
           buf[0] == 'G' ? "the code healed itself while running." : "NOT healed.");
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) { usage(); return 0; }
    const char *cmd = argv[1];

    if (!strcmp(cmd, "run"))   return cmd_run(argc, argv);
    if (!strcmp(cmd, "map"))   return cmd_map(argc, argv);
    if (!strcmp(cmd, "ps"))    return cmd_ps();
    if (!strcmp(cmd, "guard")) return cmd_guard();
    if (!strcmp(cmd, "spec"))  return cmd_spec();
    if (!strcmp(cmd, "eat"))   return cmd_eat(argc >= 3 ? argv[2] : "text");
    if (!strcmp(cmd, "help") || !strcmp(cmd, "-h")) { usage(); return 0; }
    if (kind_from_name(cmd) >= 0) return cmd_eat(cmd);

    printf("splinter: unknown command '%s'\n", cmd);
    usage();
    return 1;
}
