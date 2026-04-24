#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

static int failed = 0;
static void ok(const char *s)   { printf("  [OK]  %s\n", s); }
static void fail(const char *s) { printf("  [FAIL] %s\n", s); failed = 1; }

static int file_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

static const char *resolve_etc(const char *name, char *buf, size_t bufsz)
{
    const char *prefixes[] = { "/mnt/etc/", "/etc/", NULL };
    for (int i = 0; prefixes[i]; i++) {
        size_t pl = strlen(prefixes[i]);
        size_t nl = strlen(name);
        if (pl + nl + 1 > bufsz) continue;
        memcpy(buf, prefixes[i], pl);
        memcpy(buf + pl, name, nl + 1);
        if (file_exists(buf)) return buf;
    }
    return NULL;
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    puts("--- test_files ---");

    char hostname_path[256], passwd_path[256];
    const char *hostname = resolve_etc("hostname", hostname_path, sizeof(hostname_path));
    const char *passwd   = resolve_etc("passwd",   passwd_path,   sizeof(passwd_path));

    if (!hostname) printf("  [WARN] hostname not found in /etc or /mnt/etc\n");
    if (!passwd)   printf("  [WARN] passwd not found in /etc or /mnt/etc\n");

    {
        const char *msg = "hello from test_files\n";
        ssize_t w = write(1, msg, strlen(msg));
        if (w == (ssize_t)strlen(msg)) ok("write to stdout");
        else fail("write to stdout");
    }
    {
        const char *msg = "stderr line\n";
        ssize_t w = write(2, msg, strlen(msg));
        if (w == (ssize_t)strlen(msg)) ok("write to stderr");
        else fail("write to stderr");
    }

    if (hostname) {
        int fd = open(hostname, O_RDONLY);
        if (fd >= 0) {
            ok("open hostname");
            char buf[64];
            memset(buf, 0, sizeof(buf));
            ssize_t r = read(fd, buf, sizeof(buf) - 1);
            if (r > 0) { printf("  hostname = '%s'\n", buf); ok("read hostname"); }
            else fail("read hostname");
            struct stat st;
            if (fstat(fd, &st) == 0) {
                printf("  fstat: size=%lu\n", (unsigned long)st.st_size);
                ok("fstat");
            } else fail("fstat");
            close(fd);
            ok("close");
        } else fail("open hostname");
    }

    if (passwd) {
        struct stat st;
        if (stat(passwd, &st) == 0) {
            printf("  stat: ino=%lu size=%lu\n",
                   (unsigned long)st.st_ino, (unsigned long)st.st_size);
            ok("stat passwd");
        } else fail("stat passwd");
    }

    {
        int fd = open("/no/such/file", O_RDONLY);
        if (fd < 0) ok("open nonexistent returns error");
        else { close(fd); fail("open nonexistent should fail"); }
    }

    if (hostname) {
        int fd = open(hostname, O_RDONLY);
        if (fd >= 0) {
            char buf[4]; memset(buf, 0, sizeof(buf));
            read(fd, buf, 2);
            off_t pos = lseek(fd, 0, SEEK_CUR);
            if (pos == 2) ok("lseek SEEK_CUR");
            else fail("lseek SEEK_CUR");
            lseek(fd, 0, SEEK_SET);
            char buf2[4]; memset(buf2, 0, sizeof(buf2));
            read(fd, buf2, 2);
            if (buf[0] == buf2[0] && buf[1] == buf2[1]) ok("lseek SEEK_SET re-read");
            else fail("lseek SEEK_SET re-read");
            close(fd);
        } else fail("open for lseek test");
    }

    if (hostname) {
        int fd = open(hostname, O_RDONLY);
        if (fd >= 0) {
            int fd2 = dup(fd);
            if (fd2 >= 0 && fd2 != fd) {
                ok("dup");
                char buf[32]; memset(buf, 0, sizeof(buf));
                ssize_t r = read(fd2, buf, sizeof(buf) - 1);
                if (r > 0) ok("read from dup'd fd");
                else fail("read from dup'd fd");
                close(fd2);
            } else fail("dup");
            close(fd);
        } else fail("open for dup test");
    }

    if (hostname) {
        int fd = open(hostname, O_RDONLY);
        if (fd >= 0) {
            int r = dup2(fd, 10);
            if (r == 10) {
                ok("dup2");
                char buf[32]; memset(buf, 0, sizeof(buf));
                ssize_t n = read(10, buf, sizeof(buf) - 1);
                if (n > 0) ok("read from dup2 fd");
                else fail("read from dup2 fd");
                close(10);
            } else fail("dup2");
            close(fd);
        } else fail("open for dup2 test");
    }

    {
        int fd = open("/dev/null", O_WRONLY);
        if (fd >= 0) {
            ssize_t w = write(fd, "garbage", 7);
            if (w == 7) ok("write to /dev/null");
            else fail("write to /dev/null");
            close(fd);
        } else fail("open /dev/null");
    }
    {
        int fd = open("/dev/zero", O_RDONLY);
        if (fd >= 0) {
            char buf[8]; memset(buf, 0xFF, 8);
            ssize_t r = read(fd, buf, 8);
            int all_zero = 1;
            for (int i = 0; i < 8; i++) if (buf[i]) all_zero = 0;
            if (r == 8 && all_zero) ok("read from /dev/zero");
            else fail("read from /dev/zero");
            close(fd);
        } else fail("open /dev/zero");
    }

    puts("--- test_files done ---");
    return failed ? 1 : 0;
}
