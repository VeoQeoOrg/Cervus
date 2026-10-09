#include "stdio_impl.h"

int setvbuf(FILE *restrict f, char *restrict buf, int type, size_t size)
{
	f->lbf = EOF;

	if (type == _IONBF) {
		f->buf_size = 0;
	} else if (type == _IOLBF || type == _IOFBF) {
		if (buf && size >= UNGET) {
			f->buf = (void *)(buf + UNGET);
			f->buf_size = size - UNGET;
		}
		if (type == _IOLBF && f->buf_size)
			f->lbf = '\n';
	} else {
		return -1;
	}

	f->flags |= F_SVB;

	return 0;
}
