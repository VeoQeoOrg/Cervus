#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <string.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <libcervus.h>
#include "stdio_impl.h"

#define MAXTRIES 100

char *tmpnam(char *buf)
{
	static char internal[L_tmpnam];
	char s[] = "/tmp/tmpnam_XXXXXX";
	int try;
	int r;
	for (try=0; try<MAXTRIES; try++) {
		__randname(s+12);
		r = syscall3(SYS_READLINK, s, (char[1]){0}, 1);
		if (r == -ENOENT) return strcpy(buf ? buf : internal, s);
	}
	return 0;
}
