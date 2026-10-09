#include "stdio_impl.h"

int __towrite(FILE *f)
{
	f->mode |= f->mode-1;
	if (f->flags & F_NOWR) {
		f->flags |= F_ERR;
		return EOF;
	}

	f->rpos = f->rend = 0;

	f->wpos = f->wbase = f->buf;
	f->wend = f->buf + f->buf_size;

	return 0;
}

hidden void __towrite_needs_stdio_exit()
{
	__stdio_exit_needed();
}
