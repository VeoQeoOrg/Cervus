#ifndef _VALUES_H
#define _VALUES_H

#include <limits.h>
#include <float.h>

#define CHARBITS  CHAR_BIT
#define BITSPERBYTE CHAR_BIT
#define BITS(type) (CHAR_BIT * (int)sizeof(type))

#define SHORTBITS BITS(short)
#define INTBITS   BITS(int)
#define LONGBITS  BITS(long)
#define PTRBITS   BITS(char *)
#define DOUBLEBITS BITS(double)
#define FLOATBITS BITS(float)

#define MINSHORT SHRT_MIN
#define MININT   INT_MIN
#define MINLONG  LONG_MIN
#define MAXSHORT SHRT_MAX
#define MAXINT   INT_MAX
#define MAXLONG  LONG_MAX

#define MAXDOUBLE DBL_MAX
#define MAXFLOAT  FLT_MAX
#define MINDOUBLE DBL_MIN
#define MINFLOAT  FLT_MIN

#endif
