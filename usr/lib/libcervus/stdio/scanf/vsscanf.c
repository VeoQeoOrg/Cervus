#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "../../musl/shgetc.h"

int vsscanf(const char *restrict str, const char *restrict fmt, va_list ap)
{
	SHFILE f;
	sh_fromstring(&f, str);
	f.rend = (unsigned char *)str + strlen(str);
	return __cervus_vscan(&f, fmt, ap);
}
