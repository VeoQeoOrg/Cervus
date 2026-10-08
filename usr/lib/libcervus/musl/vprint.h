#ifndef _CERVUS_VPRINT_H
#define _CERVUS_VPRINT_H

#include <stdio.h>
#include <stdarg.h>
#include <stddef.h>
#include "musl.h"

typedef struct {
	char *buf;
	size_t size;
	size_t pos;
	FILE *file;
	int err;
} PSINK;

hidden int __cervus_vprint(PSINK *restrict, const char *restrict, va_list);

#endif
