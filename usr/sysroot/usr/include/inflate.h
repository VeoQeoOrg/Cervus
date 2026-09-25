#ifndef _CERVUS_INFLATE_H
#define _CERVUS_INFLATE_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

int raw_inflate(const uint8_t *in, size_t inlen, uint8_t **out, size_t *outlen);
int zlib_inflate(const uint8_t *in, size_t inlen, uint8_t **out, size_t *outlen);
int gunzip(const uint8_t *in, size_t inlen, uint8_t **out, size_t *outlen);

typedef void (*inflate_progress_fn)(void *ctx, size_t done, size_t total);
int gunzip_progress(const uint8_t *in, size_t inlen, uint8_t **out, size_t *outlen,
                    inflate_progress_fn progress, void *ctx);

int raw_inflate_used(const uint8_t *in, size_t inlen, uint8_t **out, size_t *outlen,
                     size_t *used);
int zlib_inflate_used(const uint8_t *in, size_t inlen, uint8_t **out, size_t *outlen,
                      size_t *used);

int raw_deflate(const uint8_t *in, size_t inlen, uint8_t **out, size_t *outlen);
int zlib_deflate(const uint8_t *in, size_t inlen, uint8_t **out, size_t *outlen);

#ifdef __cplusplus
}
#endif
#endif
