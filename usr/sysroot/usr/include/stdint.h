#ifndef _STDINT_H
#define _STDINT_H

typedef signed char        int8_t;
typedef unsigned char      uint8_t;
typedef short              int16_t;
typedef unsigned short     uint16_t;
typedef int                int32_t;
typedef unsigned int       uint32_t;
typedef long               int64_t;
typedef unsigned long      uint64_t;

typedef long               intptr_t;
typedef unsigned long      uintptr_t;

typedef long               intmax_t;
typedef unsigned long      uintmax_t;

#define INT8_MIN   (-128)
#define INT8_MAX   (127)
#define INT16_MIN  (-32768)
#define INT16_MAX  (32767)
#define INT32_MIN  (-2147483647 - 1)
#define INT32_MAX  (2147483647)
#define INT64_MIN  (-9223372036854775807L - 1)
#define INT64_MAX  (9223372036854775807L)

#define UINT8_MAX  (0xFFu)
#define UINT16_MAX (0xFFFFu)
#define UINT32_MAX (0xFFFFFFFFu)
#define UINT64_MAX (0xFFFFFFFFFFFFFFFFUL)

#define INTPTR_MIN  INT64_MIN
#define INTPTR_MAX  INT64_MAX
#define UINTPTR_MAX UINT64_MAX

#define SIZE_MAX    UINT64_MAX

#define INT8_C(v)   v
#define INT16_C(v)  v
#define INT32_C(v)  v
#define INT64_C(v)  v ## L
#define UINT8_C(v)  v ## u
#define UINT16_C(v) v ## u
#define UINT32_C(v) v ## u
#define UINT64_C(v) v ## UL

#endif
