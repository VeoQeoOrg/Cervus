#include "stdio_impl.h"

void flockfile(FILE *f)
{
	if (!ftrylockfile(f)) return;
	__lockfile(f);
	__register_locked_file(f);
}
