#include <spawn.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/wait.h>
#include <libcervus.h>

enum { SA_OPEN, SA_CLOSE, SA_DUP2, SA_CHDIR, SA_FCHDIR, SA_CLOSEFROM };

struct __spawn_action {
    int    kind;
    int    fd;
    int    newfd;
    int    oflag;
    mode_t mode;
    char  *path;
};

int posix_spawn_file_actions_init(posix_spawn_file_actions_t *fa)
{
    fa->__used = 0;
    fa->__allocated = 0;
    fa->__actions = NULL;
    return 0;
}

int posix_spawn_file_actions_destroy(posix_spawn_file_actions_t *fa)
{
    for (int i = 0; i < fa->__used; i++) free(fa->__actions[i].path);
    free(fa->__actions);
    fa->__actions = NULL;
    fa->__used = fa->__allocated = 0;
    return 0;
}

static struct __spawn_action *add(posix_spawn_file_actions_t *fa)
{
    if (fa->__used == fa->__allocated) {
        int n = fa->__allocated ? fa->__allocated * 2 : 8;
        struct __spawn_action *a = realloc(fa->__actions, (size_t)n * sizeof(*a));
        if (!a) return NULL;
        fa->__actions = a;
        fa->__allocated = n;
    }
    struct __spawn_action *a = &fa->__actions[fa->__used];
    memset(a, 0, sizeof(*a));
    return a;
}

static int bad_fd(int fd) { return fd < 0 || fd >= 1024; }

int posix_spawn_file_actions_addopen(posix_spawn_file_actions_t *fa, int fd, const char *path,
                                     int oflag, mode_t mode)
{
    if (bad_fd(fd)) return EBADF;
    struct __spawn_action *a = add(fa);
    if (!a) return ENOMEM;
    a->path = strdup(path);
    if (!a->path) return ENOMEM;
    a->kind = SA_OPEN; a->fd = fd; a->oflag = oflag; a->mode = mode;
    fa->__used++;
    return 0;
}

int posix_spawn_file_actions_addclose(posix_spawn_file_actions_t *fa, int fd)
{
    if (bad_fd(fd)) return EBADF;
    struct __spawn_action *a = add(fa);
    if (!a) return ENOMEM;
    a->kind = SA_CLOSE; a->fd = fd;
    fa->__used++;
    return 0;
}

int posix_spawn_file_actions_adddup2(posix_spawn_file_actions_t *fa, int fd, int newfd)
{
    if (bad_fd(fd) || bad_fd(newfd)) return EBADF;
    struct __spawn_action *a = add(fa);
    if (!a) return ENOMEM;
    a->kind = SA_DUP2; a->fd = fd; a->newfd = newfd;
    fa->__used++;
    return 0;
}

int posix_spawn_file_actions_addchdir_np(posix_spawn_file_actions_t *fa, const char *path)
{
    struct __spawn_action *a = add(fa);
    if (!a) return ENOMEM;
    a->path = strdup(path);
    if (!a->path) return ENOMEM;
    a->kind = SA_CHDIR;
    fa->__used++;
    return 0;
}

int posix_spawn_file_actions_addfchdir_np(posix_spawn_file_actions_t *fa, int fd)
{
    if (bad_fd(fd)) return EBADF;
    struct __spawn_action *a = add(fa);
    if (!a) return ENOMEM;
    a->kind = SA_FCHDIR; a->fd = fd;
    fa->__used++;
    return 0;
}

int posix_spawn_file_actions_addclosefrom_np(posix_spawn_file_actions_t *fa, int from)
{
    if (bad_fd(from)) return EBADF;
    struct __spawn_action *a = add(fa);
    if (!a) return ENOMEM;
    a->kind = SA_CLOSEFROM; a->fd = from;
    fa->__used++;
    return 0;
}

int posix_spawnattr_init(posix_spawnattr_t *a)
{
    memset(a, 0, sizeof(*a));
    sigemptyset(&a->__sigdefault);
    sigemptyset(&a->__sigmask);
    return 0;
}

int posix_spawnattr_destroy(posix_spawnattr_t *a) { (void)a; return 0; }

int posix_spawnattr_getflags(const posix_spawnattr_t *a, short *flags) { *flags = (short)a->__flags; return 0; }

int posix_spawnattr_setflags(posix_spawnattr_t *a, short flags)
{
    if (flags & ~0xff) return EINVAL;
    a->__flags = flags;
    return 0;
}

int posix_spawnattr_getpgroup(const posix_spawnattr_t *a, pid_t *pgrp) { *pgrp = a->__pgrp; return 0; }
int posix_spawnattr_setpgroup(posix_spawnattr_t *a, pid_t pgrp) { a->__pgrp = pgrp; return 0; }
int posix_spawnattr_getsigdefault(const posix_spawnattr_t *a, sigset_t *s) { *s = a->__sigdefault; return 0; }
int posix_spawnattr_setsigdefault(posix_spawnattr_t *a, const sigset_t *s) { a->__sigdefault = *s; return 0; }
int posix_spawnattr_getsigmask(const posix_spawnattr_t *a, sigset_t *s) { *s = a->__sigmask; return 0; }
int posix_spawnattr_setsigmask(posix_spawnattr_t *a, const sigset_t *s) { a->__sigmask = *s; return 0; }
int posix_spawnattr_getschedparam(const posix_spawnattr_t *a, struct sched_param *p) { *p = a->__param; return 0; }
int posix_spawnattr_setschedparam(posix_spawnattr_t *a, const struct sched_param *p) { a->__param = *p; return 0; }
int posix_spawnattr_getschedpolicy(const posix_spawnattr_t *a, int *policy) { *policy = a->__policy; return 0; }
int posix_spawnattr_setschedpolicy(posix_spawnattr_t *a, int policy) { a->__policy = policy; return 0; }

static void child_fail(int pipefd, int err)
{
    ssize_t r;
    do { r = write(pipefd, &err, sizeof(err)); } while (r < 0 && errno == EINTR);
    _exit(127);
}

static void exec_path(const char *file, char *const argv[], char *const envp[], int use_path, int pipefd)
{
    if (!use_path || strchr(file, '/')) {
        execve(file, argv, envp);
        child_fail(pipefd, errno);
    }
    const char *path = getenv("PATH");
    if (!path || !*path) path = "/bin:/usr/bin:/apps";
    size_t flen = strlen(file);
    char buf[1024];
    int seen_eacces = 0;
    const char *p = path;
    for (;;) {
        const char *end = strchr(p, ':');
        size_t dlen = end ? (size_t)(end - p) : strlen(p);
        if (dlen + 1 + flen + 1 <= sizeof(buf)) {
            if (dlen) {
                memcpy(buf, p, dlen);
                buf[dlen] = '/';
                memcpy(buf + dlen + 1, file, flen + 1);
            } else {
                memcpy(buf, file, flen + 1);
            }
            execve(buf, argv, envp);
            if (errno == EACCES) seen_eacces = 1;
            else if (errno != ENOENT && errno != ENOTDIR) child_fail(pipefd, errno);
        }
        if (!end) break;
        p = end + 1;
    }
    child_fail(pipefd, seen_eacces ? EACCES : ENOENT);
}

static void run_child(const char *file, const posix_spawn_file_actions_t *fa, const posix_spawnattr_t *attr,
                      char *const argv[], char *const envp[], int use_path, int pipefd)
{
    if (attr) {
        int fl = attr->__flags;
        if ((fl & POSIX_SPAWN_SETSID) && setsid() < 0) child_fail(pipefd, errno);
        if ((fl & POSIX_SPAWN_SETPGROUP) && setpgid(0, attr->__pgrp) < 0) child_fail(pipefd, errno);
        if (fl & POSIX_SPAWN_RESETIDS) {
            if (setgid(getgid()) < 0) child_fail(pipefd, errno);
            if (setuid(getuid()) < 0) child_fail(pipefd, errno);
        }
        if (fl & POSIX_SPAWN_SETSIGDEF) {
            struct sigaction sa;
            memset(&sa, 0, sizeof(sa));
            sa.sa_handler = SIG_DFL;
            for (int s = 1; s < NSIG; s++)
                if (sigismember(&attr->__sigdefault, s) == 1) sigaction(s, &sa, NULL);
        }
        if ((fl & POSIX_SPAWN_SETSIGMASK) && sigprocmask(SIG_SETMASK, &attr->__sigmask, NULL) < 0)
            child_fail(pipefd, errno);
    }
    for (int i = 0; fa && i < fa->__used; i++) {
        const struct __spawn_action *a = &fa->__actions[i];
        switch (a->kind) {
        case SA_OPEN: {
            int fd = open(a->path, a->oflag, a->mode);
            if (fd < 0) child_fail(pipefd, errno);
            if (fd != a->fd) {
                if (dup2(fd, a->fd) < 0) child_fail(pipefd, errno);
                close(fd);
            }
            break;
        }
        case SA_CLOSE:
            close(a->fd);
            break;
        case SA_DUP2:
            if (a->fd == a->newfd) {
                int fl = fcntl(a->fd, F_GETFD);
                if (fl < 0 || fcntl(a->fd, F_SETFD, fl & ~FD_CLOEXEC) < 0) child_fail(pipefd, errno);
            } else if (dup2(a->fd, a->newfd) < 0) {
                child_fail(pipefd, errno);
            }
            break;
        case SA_CHDIR:
            if (chdir(a->path) < 0) child_fail(pipefd, errno);
            break;
        case SA_FCHDIR:
            if (fchdir(a->fd) < 0) child_fail(pipefd, errno);
            break;
        case SA_CLOSEFROM:
            for (int fd = a->fd; fd < 1024; fd++)
                if (fd != pipefd) close(fd);
            break;
        }
    }
    exec_path(file, argv, envp, use_path, pipefd);
}

static int do_spawn(pid_t *pid, const char *file, const posix_spawn_file_actions_t *fa,
                    const posix_spawnattr_t *attr, char *const argv[], char *const envp[], int use_path)
{
    extern char **environ;
    if (!envp) envp = environ;
    int p[2];
    if (pipe2(p, O_CLOEXEC) < 0) return errno;
    int saved = errno;
    pid_t c = fork();
    if (c < 0) {
        int e = errno;
        close(p[0]);
        close(p[1]);
        return e;
    }
    if (c == 0) {
        close(p[0]);
        run_child(file, fa, attr, argv, envp, use_path, p[1]);
    }
    close(p[1]);
    int err = 0;
    ssize_t n;
    do { n = read(p[0], &err, sizeof(err)); } while (n < 0 && errno == EINTR);
    close(p[0]);
    if (n == (ssize_t)sizeof(err) && err) {
        int st;
        while (waitpid(c, &st, 0) < 0 && errno == EINTR) {}
        __cervus_set_errno(saved);
        return err;
    }
    if (pid) *pid = c;
    __cervus_set_errno(saved);
    return 0;
}

int posix_spawn(pid_t *pid, const char *path, const posix_spawn_file_actions_t *fa,
                const posix_spawnattr_t *attr, char *const argv[], char *const envp[])
{
    return do_spawn(pid, path, fa, attr, argv, envp, 0);
}

int posix_spawnp(pid_t *pid, const char *file, const posix_spawn_file_actions_t *fa,
                 const posix_spawnattr_t *attr, char *const argv[], char *const envp[])
{
    return do_spawn(pid, file, fa, attr, argv, envp, 1);
}
