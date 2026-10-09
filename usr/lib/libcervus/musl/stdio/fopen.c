#include "stdio_impl.h"
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

FILE *fopen(const char *restrict filename, const char *restrict mode)
{
	FILE *f;
	int fd;
	int flags;

	if (!strchr("rwa", *mode)) {
		errno = EINVAL;
		return 0;
	}

	flags = __fmodeflags(mode);

	fd = open(filename, flags, 0666);
	if (fd < 0) return 0;
	if (flags & O_CLOEXEC)
		fcntl(fd, F_SETFD, FD_CLOEXEC);

	f = __fdopen(fd, mode);
	if (f) return f;

	close(fd);
	return 0;
}
