#include "stdio_impl.h"
#include <errno.h>

int __fseeko_unlocked(FILE *f, off_t off, int whence)
{

	if (whence != SEEK_CUR && whence != SEEK_SET && whence != SEEK_END) {
		errno = EINVAL;
		return -1;
	}

	if (whence == SEEK_CUR && f->rend) off -= f->rend - f->rpos;

	if (f->wpos != f->wbase) {
		f->write(f, 0, 0);
		if (!f->wpos) return -1;
	}

	f->wpos = f->wbase = f->wend = 0;

	if (f->seek(f, off, whence) < 0) return -1;

	f->rpos = f->rend = 0;
	f->flags &= ~F_EOF;

	return 0;
}

int __fseeko(FILE *f, off_t off, int whence)
{
	int result;
	FLOCK(f);
	result = __fseeko_unlocked(f, off, whence);
	FUNLOCK(f);
	return result;
}

int fseek(FILE *f, long off, int whence)
{
	return __fseeko(f, off, whence);
}

weak_alias(__fseeko, fseeko);
