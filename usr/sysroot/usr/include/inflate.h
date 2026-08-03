#ifndef _CERVUS_INFLATE_H
#define _CERVUS_INFLATE_H

#include <stdint.h>
#include <stddef.h>

int raw_inflate(const uint8_t *in, size_t inlen, uint8_t **out, size_t *outlen);
int zlib_inflate(const uint8_t *in, size_t inlen, uint8_t **out, size_t *outlen);
int gunzip(const uint8_t *in, size_t inlen, uint8_t **out, size_t *outlen);

#endif
