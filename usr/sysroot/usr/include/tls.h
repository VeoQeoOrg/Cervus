#ifndef _CERVUS_TLS_H
#define _CERVUS_TLS_H

#include <stddef.h>

typedef struct tls_conn tls_conn;

tls_conn  *tls_client_new(int fd, const char *hostname);
void       tls_set_insecure(tls_conn *c);
int        tls_handshake(tls_conn *c);
int        tls_write(tls_conn *c, const void *buf, size_t len);
int        tls_read(tls_conn *c, void *buf, size_t len);
void       tls_free(tls_conn *c);
const char *tls_error(tls_conn *c);

#endif
