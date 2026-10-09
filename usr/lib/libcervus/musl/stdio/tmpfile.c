#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "stdio_impl.h"

FILE *tmpfile(void)
{
	char s[] = "/tmp/tmpfile_XXXXXX";
	FILE *f;
	int fd = mkstemp(s);
	if (fd < 0) return 0;
	unlink(s);
	f = __fdopen(fd, "w+");
	if (!f) close(fd);
	return f;
}
