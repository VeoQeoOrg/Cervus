#ifndef PLEVAL_H
#define PLEVAL_H

#include "../musl.h"

hidden unsigned long __pleval(const char *, unsigned long);
hidden const char *__mo_lookup(const void *, size_t, const char *);

#endif
