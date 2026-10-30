#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <tls.h>
#include "darwin.h"

#define noErr                  0
#define errSecParam            (-50)
#define errSSLProtocol         (-9800)
#define errSSLNegotiation      (-9801)
#define errSSLXCertChainInvalid (-9807)
#define errSSLClosedGraceful   (-9805)
#define errSSLClosedAbort      (-9806)
#define errSSLWouldBlock       (-9803)
#define errSSLBadConfiguration (-9848)

typedef int (*ssl_io)(void *conn, void *data, size_t *len);

struct sslctx {
    uint8_t hdr[sizeof(void *) * 3];
    tls_conn *tls;
    ssl_io rd, wr;
    void *conn;
    char host[256];
    int state;
    int closed;
};

static void ctx_free(void *o)
{
    struct sslctx *c = o;
    if (c->tls)
        tls_free(c->tls);
    c->tls = NULL;
}

static long io_read(void *ctx, void *buf, size_t len)
{
    struct sslctx *c = ctx;
    for (;;) {
        size_t n = len;
        int r = c->rd(c->conn, buf, &n);
        if (n > 0)
            return (long)n;
        if (r != errSSLWouldBlock)
            return r == errSSLClosedGraceful || r == noErr ? 0 : -1;
    }
}

static long io_write(void *ctx, void *buf, size_t len)
{
    struct sslctx *c = ctx;
    for (;;) {
        size_t n = len;
        int r = c->wr(c->conn, buf, &n);
        if (n > 0)
            return (long)n;
        if (r != noErr && r != errSSLWouldBlock)
            return -1;
    }
}

static struct sslctx *d_SSLCreateContext(void *alloc, int side, int type)
{
    (void)alloc;
    if (side != 1 || type != 0)
        return NULL;
    return d_cf_opaque_new(sizeof(struct sslctx), ctx_free);
}

static int d_SSLSetIOFuncs(struct sslctx *c, ssl_io rd, ssl_io wr)
{
    if (!c)
        return errSecParam;
    c->rd = rd;
    c->wr = wr;
    return noErr;
}

static int d_SSLSetConnection(struct sslctx *c, void *conn)
{
    if (!c)
        return errSecParam;
    c->conn = conn;
    return noErr;
}

static int d_SSLGetConnection(struct sslctx *c, void **conn)
{
    if (!c || !conn)
        return errSecParam;
    *conn = c->conn;
    return noErr;
}

static int d_SSLSetPeerDomainName(struct sslctx *c, const char *name, size_t len)
{
    if (!c)
        return errSecParam;
    if (len >= sizeof c->host)
        len = sizeof c->host - 1;
    memcpy(c->host, name ? name : "", name ? len : 0);
    c->host[name ? len : 0] = 0;
    return noErr;
}

static int d_SSLOk(void *c) { return c ? noErr : errSecParam; }

static int d_SSLHandshake(struct sslctx *c)
{
    if (!c || !c->rd || !c->wr)
        return errSSLBadConfiguration;
    if (c->state == 2)
        return noErr;
    if (c->state == 3)
        return errSSLXCertChainInvalid;
    c->tls = tls_client_new_io(c, io_read, io_write, c->host[0] ? c->host : NULL);
    if (!c->tls)
        return errSSLProtocol;
    if (tls_handshake(c->tls) != 0) {
        const char *e = tls_error(c->tls);
        c->state = e && strstr(e, "certificate") ? 3 : 4;
        return c->state == 3 ? errSSLXCertChainInvalid : errSSLNegotiation;
    }
    c->state = 2;
    return noErr;
}

static int d_SSLRead(struct sslctx *c, void *data, size_t len, size_t *done)
{
    *done = 0;
    if (!c || c->state != 2)
        return errSSLProtocol;
    if (c->closed)
        return errSSLClosedGraceful;
    if (!len)
        return noErr;
    int n = tls_read(c->tls, data, len);
    if (n < 0)
        return errSSLClosedAbort;
    if (n == 0) {
        c->closed = 1;
        return errSSLClosedGraceful;
    }
    *done = (size_t)n;
    return noErr;
}

static int d_SSLWrite(struct sslctx *c, const void *data, size_t len, size_t *done)
{
    *done = 0;
    if (!c || c->state != 2)
        return errSSLProtocol;
    if (len && tls_write(c->tls, data, len) != (int)len)
        return errSSLClosedAbort;
    *done = len;
    return noErr;
}

static int d_SSLClose(struct sslctx *c)
{
    if (!c)
        return errSecParam;
    if (c->tls)
        tls_free(c->tls);
    c->tls = NULL;
    c->closed = 1;
    return noErr;
}

static int d_SSLGetBufferedReadSize(struct sslctx *c, size_t *n)
{
    (void)c;
    *n = 0;
    return noErr;
}

static int d_SSLGetSessionState(struct sslctx *c, int *st)
{
    if (!c || !st)
        return errSecParam;
    *st = c->closed ? 3 : c->state == 2 ? 2 : c->state ? 4 : 0;
    return noErr;
}

static int d_SSLCopyPeerTrust(struct sslctx *c, void **trust)
{
    if (!c || !trust)
        return errSecParam;
    *trust = NULL;
    if (!c->tls)
        return noErr;
    int n = tls_peer_cert(c->tls, 0, NULL, NULL);
    const uint8_t *der[12];
    size_t len[12];
    if (n > 12)
        n = 12;
    for (int i = 0; i < n; i++)
        tls_peer_cert(c->tls, i, &der[i], &len[i]);
    *trust = d_sec_trust_from_der(der, len, n, c->host[0] ? c->host : NULL);
    return noErr;
}

static int d_SSLGetNegotiatedProtocolVersion(struct sslctx *c, int *v)
{
    if (!c || !v)
        return errSecParam;
    *v = 10;
    return noErr;
}

static int d_SSLGetNegotiatedCipher(struct sslctx *c, int *v)
{
    if (!c || !v)
        return errSecParam;
    *v = 0x1301;
    return noErr;
}

DSYM("_SSLCreateContext", d_SSLCreateContext);
DSYM("_SSLSetIOFuncs", d_SSLSetIOFuncs);
DSYM("_SSLSetConnection", d_SSLSetConnection);
DSYM("_SSLGetConnection", d_SSLGetConnection);
DSYM("_SSLSetPeerDomainName", d_SSLSetPeerDomainName);
DSYM("_SSLSetProtocolVersionMin", d_SSLOk);
DSYM("_SSLSetProtocolVersionMax", d_SSLOk);
DSYM("_SSLSetSessionOption", d_SSLOk);
DSYM("_SSLSetCertificate", d_SSLOk);
DSYM("_SSLSetALPNProtocols", d_SSLOk);
DSYM("_SSLSetEnabledCiphers", d_SSLOk);
DSYM("_SSLHandshake", d_SSLHandshake);
DSYM("_SSLRead", d_SSLRead);
DSYM("_SSLWrite", d_SSLWrite);
DSYM("_SSLClose", d_SSLClose);
DSYM("_SSLGetBufferedReadSize", d_SSLGetBufferedReadSize);
DSYM("_SSLGetSessionState", d_SSLGetSessionState);
DSYM("_SSLCopyPeerTrust", d_SSLCopyPeerTrust);
DSYM("_SSLGetNegotiatedProtocolVersion", d_SSLGetNegotiatedProtocolVersion);
DSYM("_SSLGetNegotiatedCipher", d_SSLGetNegotiatedCipher);
