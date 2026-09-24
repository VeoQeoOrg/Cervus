#ifndef _CERVUS_PTY_H
#define _CERVUS_PTY_H

#include <sys/types.h>
#include <sys/ioctl.h>
#include <termios.h>

#ifdef __cplusplus
extern "C" {
#endif

int   openpty(int *master, int *slave, char *name,
              const struct termios *termp, const struct winsize *winp);
pid_t forkpty(int *master, char *name,
              const struct termios *termp, const struct winsize *winp);
int   login_tty(int fd);

#ifdef __cplusplus
}
#endif
#endif
