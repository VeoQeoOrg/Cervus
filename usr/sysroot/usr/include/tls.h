#ifndef _CERVUS_TLS_H
#define _CERVUS_TLS_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

typedef struct tls_conn tls_conn;

typedef long (*tls_io_fn)(void *ctx, void *buf, size_t len);

tls_conn  *tls_client_new(int fd, const char *hostname);
tls_conn  *tls_client_new_io(void *ctx, tls_io_fn rd, tls_io_fn wr, const char *hostname);
void       tls_set_insecure(tls_conn *c);
int        tls_handshake(tls_conn *c);
int        tls_write(tls_conn *c, const void *buf, size_t len);
int        tls_read(tls_conn *c, void *buf, size_t len);
int        tls_peer_cert(tls_conn *c, int i, const unsigned char **der, size_t *len);
void       tls_free(tls_conn *c);
const char *tls_error(tls_conn *c);

#ifdef __cplusplus
}
#endif
#endif
