#ifndef _UNISTD_H
#define _UNISTD_H

#include <stddef.h>
#include <sys/types.h>

#define STDIN_FILENO   0
#define STDOUT_FILENO  1
#define STDERR_FILENO  2

#define SEEK_SET  0
#define SEEK_CUR  1
#define SEEK_END  2

ssize_t read(int fd, void *buf, size_t n);
ssize_t write(int fd, const void *buf, size_t n);
int     close(int fd);
off_t   lseek(int fd, off_t off, int whence);
int     dup(int fd);
int     dup2(int oldfd, int newfd);
int     pipe(int fds[2]);
int     unlink(const char *path);
int     rmdir(const char *path);

pid_t   getpid(void);
pid_t   getppid(void);
uid_t   getuid(void);
gid_t   getgid(void);
int     setuid(uid_t uid);
int     setgid(gid_t gid);
pid_t   fork(void);
int     execve(const char *path, char *const argv[], char *const envp[]);
int     execv(const char *path, char *const argv[]);
int     execvp(const char *file, char *const argv[]);
void    _exit(int status) __attribute__((noreturn));

unsigned int sleep(unsigned int sec);
int          usleep(unsigned int usec);

char *getcwd(char *buf, size_t size);

void *sbrk(intptr_t increment);
int   brk(void *addr);

void  sched_yield_cervus(void);

int   isatty(int fd);

extern char *optarg;
extern int   optind;
extern int   optopt;
extern int   opterr;
int          getopt(int argc, char *const argv[], const char *optstring);

#endif