#ifndef _INTTYPES_H
#define _INTTYPES_H

#include <stdint.h>

#define PRId8    "d"
#define PRId16   "d"
#define PRId32   "d"
#define PRId64   "ld"

#define PRIu8    "u"
#define PRIu16   "u"
#define PRIu32   "u"
#define PRIu64   "lu"

#define PRIx8    "x"
#define PRIx16   "x"
#define PRIx32   "x"
#define PRIx64   "lx"

#define PRIX8    "X"
#define PRIX16   "X"
#define PRIX32   "X"
#define PRIX64   "lX"

#define PRIi8    "i"
#define PRIi16   "i"
#define PRIi32   "i"
#define PRIi64   "li"

#define PRIo8    "o"
#define PRIo16   "o"
#define PRIo32   "o"
#define PRIo64   "lo"

#define PRIdMAX  "ld"
#define PRIuMAX  "lu"
#define PRIxMAX  "lx"
#define PRIiMAX  "li"

#define PRIdPTR  "ld"
#define PRIuPTR  "lu"
#define PRIxPTR  "lx"

#define SCNd8    "hhd"
#define SCNd16   "hd"
#define SCNd32   "d"
#define SCNd64   "ld"

#define SCNu8    "hhu"
#define SCNu16   "hu"
#define SCNu32   "u"
#define SCNu64   "lu"

#define SCNx8    "hhx"
#define SCNx16   "hx"
#define SCNx32   "x"
#define SCNx64   "lx"

typedef struct {
    intmax_t  quot;
    intmax_t  rem;
} imaxdiv_t;

static inline intmax_t imaxabs(intmax_t j) {
    return j < 0 ? -j : j;
}

static inline imaxdiv_t imaxdiv(intmax_t numer, intmax_t denom) {
    imaxdiv_t r;
    r.quot = numer / denom;
    r.rem  = numer % denom;
    return r;
}

#endif