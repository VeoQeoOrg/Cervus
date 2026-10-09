#include "stdio_impl.h"
#include <stdlib.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/syscall.h>
#include <libcervus.h>

FILE *__fdopen(int fd, const char *mode)
{
	FILE *f;
	struct winsize wsz;

	if (!strchr("rwa", *mode)) {
		errno = EINVAL;
		return 0;
	}

	if (!(f=malloc(sizeof *f + UNGET + STDIO_BUFSZ))) return 0;

	memset(f, 0, sizeof *f);

	if (!strchr(mode, '+')) f->flags = (*mode == 'r') ? F_NOWR : F_NORD;

	if (strchr(mode, 'e')) syscall3(SYS_FCNTL, fd, F_SETFD, FD_CLOEXEC);

	if (*mode == 'a') {
		int flags = syscall3(SYS_FCNTL, fd, F_GETFL, 0);
		if (!(flags & O_APPEND))
			syscall3(SYS_FCNTL, fd, F_SETFL, flags | O_APPEND);
		f->flags |= F_APP;
	}

	f->fd = fd;
	f->buf = (unsigned char *)f + sizeof *f + UNGET;
	f->buf_size = STDIO_BUFSZ;

	f->lbf = EOF;
	if (!(f->flags & F_NOWR) && !syscall3(SYS_IOCTL, fd, TIOCGWINSZ, &wsz))
		f->lbf = '\n';

	f->read = __stdio_read;
	f->write = __stdio_write;
	f->seek = __stdio_seek;
	f->close = __stdio_close;

	if (!__cervus_threaded) f->lock = -1;

	return __ofl_add(f);
}

weak_alias(__fdopen, fdopen);
