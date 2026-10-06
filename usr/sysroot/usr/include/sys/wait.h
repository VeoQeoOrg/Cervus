#ifndef _SYS_WAIT_H
#define _SYS_WAIT_H
#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <signal.h>

#define WNOHANG    0x1
#define WUNTRACED  0x2
#define WSTOPPED   WUNTRACED
#define WEXITED    0x4
#define WCONTINUED 0x8
#define WNOWAIT    0x01000000

typedef enum { P_ALL = 0, P_PID = 1, P_PGID = 2 } idtype_t;

#define WIFEXITED(s)    (((s) & 0x7F) == 0)
#define WEXITSTATUS(s)  (((s) >> 8) & 0xFF)
#define WIFSIGNALED(s)  (((s) & 0x7F) != 0 && ((s) & 0x7F) != 0x7F)
#define WTERMSIG(s)     ((s) & 0x7F)
#define WIFSTOPPED(s)   (((s) & 0xFF) == 0x7F)
#define WSTOPSIG(s)     (((s) >> 8) & 0xFF)
#define WIFCONTINUED(s) ((s) == 0xFFFF)
#define WCOREDUMP(s)    ((s) & 0x80)

pid_t wait(int *status);
pid_t waitpid(pid_t pid, int *status, int options);
struct rusage;
pid_t wait3(int *status, int options, struct rusage *ru);
pid_t wait4(pid_t pid, int *status, int options, struct rusage *ru);
int waitid(idtype_t idtype, id_t id, siginfo_t *info, int options);

#ifdef __cplusplus
}
#endif
#endif