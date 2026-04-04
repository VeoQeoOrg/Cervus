#include "../apps/cervus_user.h"

static int failed = 0;
static void ok(const char *s)   { printf("  [OK]  %s\n", s); }
static void fail(const char *s) { printf("  [FAIL] %s\n", s); failed = 1; }

CERVUS_MAIN(test_pipe_main) {
    (void)argc; (void)argv;
    puts("--- test_pipe ---");

    int fds[2] = { -1, -1 };
    if (pipe(fds) == 0 && fds[0] >= 0 && fds[1] >= 0) {
        ok("pipe() creates two fds");
    } else {
        fail("pipe() failed");
        exit(1);
    }
    printf("  read_fd=%d write_fd=%d\n", fds[0], fds[1]);

    {
        const char *msg = "hello pipe";
        ssize_t w = write(fds[1], msg, strlen(msg));
        if (w == (ssize_t)strlen(msg)) ok("write to pipe");
        else { printf("  write returned %d\n", (int)w); fail("write to pipe"); }

        char buf[32]; memset(buf, 0, sizeof(buf));
        ssize_t r = read(fds[0], buf, sizeof(buf)-1);
        if (r == (ssize_t)strlen(msg) && strcmp(buf, msg) == 0)
            ok("read from pipe matches written data");
        else {
            printf("  got '%s' (r=%d)\n", buf, (int)r);
            fail("read from pipe");
        }
    }

    close(fds[0]); close(fds[1]);

    {
        int p[2];
        if (pipe(p) != 0) { fail("pipe for fork test"); exit(1); }

        pid_t child = fork();
        if (child < 0) { fail("fork"); exit(1); }

        if (child == 0) {
            close(p[0]);
            const char *s = "world";
            write(p[1], s, strlen(s));
            close(p[1]);
            exit(0);
        }

        close(p[1]);
        char buf[32]; memset(buf, 0, sizeof(buf));
        ssize_t r = read(p[0], buf, sizeof(buf)-1);
        close(p[0]);

        int status = 0;
        waitpid(child, &status, 0);

        if (r > 0 && strcmp(buf, "world") == 0) ok("pipe IPC parent<-child");
        else { printf("  got '%s' (r=%d)\n", buf, (int)r); fail("pipe IPC"); }
    }

    {
        int p[2];
        if (pipe(p) != 0) { fail("pipe for dup2 test"); exit(1); }

        pid_t child = fork();
        if (child < 0) { fail("fork for dup2"); exit(1); }

        if (child == 0) {
            close(p[0]);
            dup2(p[1], 1);
            close(p[1]);
            const char *s = "from child stdout\n";
            write(1, s, strlen(s));
            exit(0);
        }

        close(p[1]);
        char buf[64]; memset(buf, 0, sizeof(buf));
        ssize_t r = read(p[0], buf, sizeof(buf)-1);
        close(p[0]);

        int status = 0;
        waitpid(child, &status, 0);

        if (r > 0 && strncmp(buf, "from child stdout", 17) == 0)
            ok("dup2 redirects child stdout through pipe");
        else {
            printf("  got '%s' (r=%d)\n", buf, (int)r);
            fail("dup2 redirect");
        }
    }

    puts("--- test_pipe done ---");
    exit(failed ? 1 : 0);
}