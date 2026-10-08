#include <stdio.h>
#include <stdarg.h>
#include "../../musl/shgetc.h"

int vfscanf(FILE *restrict stream, const char *restrict fmt, va_list ap)
{
	SHFILE f;
	sh_fromfile(&f, stream);
	flockfile(stream);
	int r = __cervus_vscan(&f, fmt, ap);
	funlockfile(stream);
	return r;
}
