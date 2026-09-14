#ifndef _ALLOCA_H
#define _ALLOCA_H

#include <stddef.h>

#define alloca(size) __builtin_alloca(size)

#endif
