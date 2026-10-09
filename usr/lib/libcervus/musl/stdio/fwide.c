#include <wchar.h>
#include "stdio_impl.h"

int fwide(FILE *f, int mode)
{
	FLOCK(f);
	if (mode && !f->mode) f->mode = mode>0 ? 1 : -1;
	mode = f->mode;
	FUNLOCK(f);
	return mode;
}
