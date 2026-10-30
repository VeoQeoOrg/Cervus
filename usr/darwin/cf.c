#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <stdarg.h>
#include <strings.h>
#include <x509.h>
#include "darwin.h"

#define UTF8 0x08000100u

enum { T_NONE, T_STRING, T_DATA, T_ARRAY, T_DICT, T_NUMBER, T_BOOL, T_DATE, T_ERROR, T_CERT, T_POLICY, T_TRUST, T_LOCALE,
       T_TZ, T_OPAQUE, T_COUNT };

static const uint64_t g_typeids[] = { 0, 7, 20, 19, 18, 22, 21, 42, 30, 55, 56, 57, 13, 41, 1000 };

struct cfhdr {
    const void *isa;
    long rc;
};

struct cfstr {
    struct cfhdr h;
    char *ptr;
    long len;
};

struct cfdata {
    struct cfhdr h;
    uint8_t *ptr;
    long len;
};

struct cfarray {
    struct cfhdr h;
    void **v;
    long n, cap;
    int retains;
};

struct cfdict {
    struct cfhdr h;
    void **k, **v;
    long n, cap;
};

struct cfnum {
    struct cfhdr h;
    int64_t i;
    double d;
    int is_float;
};

struct cfdate {
    struct cfhdr h;
    double t;
};

struct cferr {
    struct cfhdr h;
    long code;
    void *desc;
};

struct seccert {
    struct cfhdr h;
    uint8_t *der;
    long len;
};

struct secpolicy {
    struct cfhdr h;
    char host[256];
};

struct sectrust {
    struct cfhdr h;
    struct cfarray *certs;
    char host[256];
    int64_t when;
};

struct cftz {
    struct cfhdr h;
    struct cfstr *name;
};

struct cfopaque {
    struct cfhdr h;
    void (*dtor)(void *);
};

static const char g_cls[T_COUNT];
char d_cfconst_class[96];
DSYM("___CFConstantStringClassReference", d_cfconst_class);

static void *d_kCFAllocatorDefault;
DSYM("_kCFAllocatorDefault", &d_kCFAllocatorDefault);
DSYM("_kCFAllocatorSystemDefault", &d_kCFAllocatorDefault);

static struct cfhdr g_true = { &g_cls[T_BOOL], -1 }, g_false = { &g_cls[T_BOOL], -1 };
static void *d_kCFBooleanTrue = &g_true, *d_kCFBooleanFalse = &g_false;
DSYM("_kCFBooleanTrue", &d_kCFBooleanTrue);
DSYM("_kCFBooleanFalse", &d_kCFBooleanFalse);

static char d_kCFTypeArrayCallBacks[40], d_kCFTypeDictionaryKeyCallBacks[48], d_kCFTypeDictionaryValueCallBacks[40];
DSYM("_kCFTypeArrayCallBacks", d_kCFTypeArrayCallBacks);
DSYM("_kCFTypeDictionaryKeyCallBacks", d_kCFTypeDictionaryKeyCallBacks);
DSYM("_kCFTypeDictionaryValueCallBacks", d_kCFTypeDictionaryValueCallBacks);
DSYM("_kCFCopyStringDictionaryKeyCallBacks", d_kCFTypeDictionaryKeyCallBacks);

static int type_of(const void *o)
{
    if (!o)
        return T_NONE;
    const void *isa = ((const struct cfhdr *)o)->isa;
    if (isa == d_cfconst_class)
        return T_STRING;
    if ((const char *)isa >= g_cls && (const char *)isa < g_cls + sizeof g_cls)
        return (int)((const char *)isa - g_cls);
    return T_NONE;
}

static int is_const(const void *o)
{
    return ((const struct cfhdr *)o)->isa == d_cfconst_class || ((const struct cfhdr *)o)->rc < 0;
}

static void *cf_new(int type, size_t size)
{
    struct cfhdr *h = calloc(1, size);
    if (h) {
        h->isa = &g_cls[type];
        h->rc = 1;
    }
    return h;
}

static void *d_CFRetain(void *o)
{
    if (o && !is_const(o))
        __atomic_add_fetch(&((struct cfhdr *)o)->rc, 1, __ATOMIC_RELAXED);
    return o;
}

static void d_CFRelease(void *o);

static void cf_free(void *o)
{
    switch (type_of(o)) {
    case T_STRING: free(((struct cfstr *)o)->ptr); break;
    case T_DATA: free(((struct cfdata *)o)->ptr); break;
    case T_ARRAY: {
        struct cfarray *a = o;
        if (a->retains)
            for (long i = 0; i < a->n; i++)
                d_CFRelease(a->v[i]);
        free(a->v);
        break;
    }
    case T_DICT: {
        struct cfdict *d = o;
        for (long i = 0; i < d->n; i++) {
            d_CFRelease(d->k[i]);
            d_CFRelease(d->v[i]);
        }
        free(d->k);
        free(d->v);
        break;
    }
    case T_ERROR: d_CFRelease(((struct cferr *)o)->desc); break;
    case T_CERT: free(((struct seccert *)o)->der); break;
    case T_TRUST: d_CFRelease(((struct sectrust *)o)->certs); break;
    case T_TZ: d_CFRelease(((struct cftz *)o)->name); break;
    case T_OPAQUE:
        if (((struct cfopaque *)o)->dtor)
            ((struct cfopaque *)o)->dtor(o);
        break;
    }
    free(o);
}

static void d_CFRelease(void *o)
{
    if (!o || is_const(o) || type_of(o) == T_NONE)
        return;
    if (__atomic_sub_fetch(&((struct cfhdr *)o)->rc, 1, __ATOMIC_ACQ_REL) == 0)
        cf_free(o);
}

static uint64_t d_CFGetTypeID(const void *o) { return g_typeids[type_of(o)]; }
static long d_CFGetRetainCount(const void *o) { return o && !is_const(o) ? ((const struct cfhdr *)o)->rc : 1; }

void d_cf_release(void *o) { d_CFRelease(o); }
void *d_cf_empty_dict(void) { return cf_new(T_DICT, sizeof(struct cfdict)); }
void *d_cf_empty_array(void) { return cf_new(T_ARRAY, sizeof(struct cfarray)); }

DSYM("_CFRetain", d_CFRetain);
DSYM("_CFRelease", d_CFRelease);
DSYM("_CFGetTypeID", d_CFGetTypeID);
DSYM("_CFGetRetainCount", d_CFGetRetainCount);

#define TYPEID_FN(fn, t) static uint64_t fn(void) { return g_typeids[t]; }
TYPEID_FN(d_CFStringGetTypeID, T_STRING)
TYPEID_FN(d_CFDataGetTypeID, T_DATA)
TYPEID_FN(d_CFArrayGetTypeID, T_ARRAY)
TYPEID_FN(d_CFDictionaryGetTypeID, T_DICT)
TYPEID_FN(d_CFNumberGetTypeID, T_NUMBER)
TYPEID_FN(d_CFBooleanGetTypeID, T_BOOL)
TYPEID_FN(d_CFDateGetTypeID, T_DATE)
DSYM("_CFStringGetTypeID", d_CFStringGetTypeID);
DSYM("_CFDataGetTypeID", d_CFDataGetTypeID);
DSYM("_CFArrayGetTypeID", d_CFArrayGetTypeID);
DSYM("_CFDictionaryGetTypeID", d_CFDictionaryGetTypeID);
DSYM("_CFNumberGetTypeID", d_CFNumberGetTypeID);
DSYM("_CFBooleanGetTypeID", d_CFBooleanGetTypeID);
DSYM("_CFDateGetTypeID", d_CFDateGetTypeID);

static struct cfstr *str_new(const char *p, long len)
{
    struct cfstr *s = cf_new(T_STRING, sizeof *s);
    if (!s)
        return NULL;
    s->ptr = malloc((size_t)len + 1);
    if (!s->ptr) {
        free(s);
        return NULL;
    }
    memcpy(s->ptr, p, (size_t)len);
    s->ptr[len] = 0;
    s->len = len;
    return s;
}

static const char *str_ptr(const void *o, long *len)
{
    const struct cfstr *s = o;
    if (type_of(o) != T_STRING) {
        *len = 0;
        return "";
    }
    *len = s->len;
    return s->ptr ? s->ptr : "";
}

static void *d_CFStringCreateWithCString(void *a, const char *c, uint32_t enc) { (void)a; (void)enc; return c ? str_new(c, (long)strlen(c)) : NULL; }
static void *d_CFStringCreateWithBytes(void *a, const uint8_t *b, long n, uint32_t enc, int ext) { (void)a; (void)enc; (void)ext; return str_new((const char *)b, n); }
static void *d_CFStringCreateCopy(void *a, void *s) { (void)a; long n; const char *p = str_ptr(s, &n); return str_new(p, n); }
static long d_CFStringGetLength(void *s) { long n; str_ptr(s, &n); return n; }

static int d_CFStringGetCString(void *s, char *buf, long size, uint32_t enc)
{
    (void)enc;
    long n;
    const char *p = str_ptr(s, &n);
    if (!buf || n + 1 > size)
        return 0;
    memcpy(buf, p, (size_t)n);
    buf[n] = 0;
    return 1;
}

static const char *d_CFStringGetCStringPtr(void *s, uint32_t enc) { (void)enc; long n; return type_of(s) == T_STRING ? str_ptr(s, &n) : NULL; }
static long d_CFStringGetMaximumSizeForEncoding(long len, uint32_t enc) { (void)enc; return len * 4; }

static void *d_CFStringCreateExternalRepresentation(void *a, void *s, uint32_t enc, uint8_t loss)
{
    (void)a;
    (void)enc;
    (void)loss;
    long n;
    const char *p = str_ptr(s, &n);
    struct cfdata *d = cf_new(T_DATA, sizeof *d);
    if (!d)
        return NULL;
    d->ptr = malloc((size_t)n + 1);
    memcpy(d->ptr, p, (size_t)n);
    d->len = n;
    return d;
}

static long d_CFStringCompare(void *a, void *b, unsigned long opts)
{
    long na, nb;
    const char *pa = str_ptr(a, &na), *pb = str_ptr(b, &nb);
    int c = (opts & 1) ? strcasecmp(pa, pb) : strcmp(pa, pb);
    return c < 0 ? -1 : c > 0 ? 1 : 0;
}

static void *d_CFStringCreateWithFormat(void *a, void *opts, void *fmt, ...)
{
    (void)a;
    (void)opts;
    long n;
    const char *f = str_ptr(fmt, &n);
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, f, ap);
    va_end(ap);
    return str_new(buf, (long)strlen(buf));
}

DSYM("_CFStringCreateWithCString", d_CFStringCreateWithCString);
DSYM("_CFStringCreateWithBytes", d_CFStringCreateWithBytes);
DSYM("_CFStringCreateCopy", d_CFStringCreateCopy);
DSYM("_CFStringGetLength", d_CFStringGetLength);
DSYM("_CFStringGetCString", d_CFStringGetCString);
DSYM("_CFStringGetCStringPtr", d_CFStringGetCStringPtr);
DSYM("_CFStringGetMaximumSizeForEncoding", d_CFStringGetMaximumSizeForEncoding);
DSYM("_CFStringCreateExternalRepresentation", d_CFStringCreateExternalRepresentation);
DSYM("_CFStringCompare", d_CFStringCompare);
DSYM("_CFStringCreateWithFormat", d_CFStringCreateWithFormat);

static void *data_new(const uint8_t *b, long n)
{
    struct cfdata *d = cf_new(T_DATA, sizeof *d);
    if (!d)
        return NULL;
    d->ptr = malloc(n > 0 ? (size_t)n : 1);
    if (n > 0)
        memcpy(d->ptr, b, (size_t)n);
    d->len = n;
    return d;
}

static void *d_CFDataCreate(void *a, const uint8_t *b, long n) { (void)a; return data_new(b, n); }
static const uint8_t *d_CFDataGetBytePtr(void *d) { return type_of(d) == T_DATA ? ((struct cfdata *)d)->ptr : NULL; }
static long d_CFDataGetLength(void *d) { return type_of(d) == T_DATA ? ((struct cfdata *)d)->len : 0; }

DSYM("_CFDataCreate", d_CFDataCreate);
DSYM("_CFDataGetBytePtr", d_CFDataGetBytePtr);
DSYM("_CFDataGetLength", d_CFDataGetLength);

static struct cfarray *array_new(int retains)
{
    struct cfarray *a = cf_new(T_ARRAY, sizeof *a);
    if (a)
        a->retains = retains;
    return a;
}

static void array_push(struct cfarray *a, void *v)
{
    if (a->n == a->cap) {
        long nc = a->cap ? a->cap * 2 : 8;
        void **nv = realloc(a->v, (size_t)nc * sizeof *nv);
        if (!nv)
            return;
        a->v = nv;
        a->cap = nc;
    }
    a->v[a->n++] = a->retains ? d_CFRetain(v) : v;
}

static void *d_CFArrayCreateMutable(void *al, long cap, const void *cb) { (void)al; (void)cap; return array_new(cb != NULL); }

static void *d_CFArrayCreate(void *al, void **vals, long n, const void *cb)
{
    (void)al;
    struct cfarray *a = array_new(cb != NULL);
    for (long i = 0; a && i < n; i++)
        array_push(a, vals[i]);
    return a;
}

static void d_CFArrayAppendValue(void *a, void *v) { if (type_of(a) == T_ARRAY) array_push(a, v); }
static long d_CFArrayGetCount(void *a) { return type_of(a) == T_ARRAY ? ((struct cfarray *)a)->n : 0; }

static void *d_CFArrayGetValueAtIndex(void *a, long i)
{
    struct cfarray *x = a;
    return type_of(a) == T_ARRAY && i >= 0 && i < x->n ? x->v[i] : NULL;
}

DSYM("_CFArrayCreateMutable", d_CFArrayCreateMutable);
DSYM("_CFArrayCreate", d_CFArrayCreate);
DSYM("_CFArrayAppendValue", d_CFArrayAppendValue);
DSYM("_CFArrayGetCount", d_CFArrayGetCount);
DSYM("_CFArrayGetValueAtIndex", d_CFArrayGetValueAtIndex);

static int cf_equal(void *a, void *b)
{
    if (a == b)
        return 1;
    int ta = type_of(a), tb = type_of(b);
    if (ta != tb)
        return 0;
    if (ta == T_STRING) {
        long na, nb;
        const char *pa = str_ptr(a, &na), *pb = str_ptr(b, &nb);
        return na == nb && !memcmp(pa, pb, (size_t)na);
    }
    if (ta == T_NUMBER)
        return ((struct cfnum *)a)->i == ((struct cfnum *)b)->i && ((struct cfnum *)a)->d == ((struct cfnum *)b)->d;
    if (ta == T_DATA)
        return ((struct cfdata *)a)->len == ((struct cfdata *)b)->len &&
               !memcmp(((struct cfdata *)a)->ptr, ((struct cfdata *)b)->ptr, (size_t)((struct cfdata *)a)->len);
    return 0;
}

static int d_CFEqual(void *a, void *b) { return cf_equal(a, b); }
DSYM("_CFEqual", d_CFEqual);

static void dict_set(struct cfdict *d, void *k, void *v)
{
    for (long i = 0; i < d->n; i++)
        if (cf_equal(d->k[i], k)) {
            d_CFRetain(v);
            d_CFRelease(d->v[i]);
            d->v[i] = v;
            return;
        }
    if (d->n == d->cap) {
        long nc = d->cap ? d->cap * 2 : 8;
        void **nk = realloc(d->k, (size_t)nc * sizeof *nk), **nv;
        if (!nk)
            return;
        d->k = nk;
        nv = realloc(d->v, (size_t)nc * sizeof *nv);
        if (!nv)
            return;
        d->v = nv;
        d->cap = nc;
    }
    d->k[d->n] = d_CFRetain(k);
    d->v[d->n] = d_CFRetain(v);
    d->n++;
}

static void *d_CFDictionaryCreate(void *al, void **keys, void **vals, long n, const void *kcb, const void *vcb)
{
    (void)al;
    (void)kcb;
    (void)vcb;
    struct cfdict *d = cf_new(T_DICT, sizeof *d);
    for (long i = 0; d && i < n; i++)
        dict_set(d, keys[i], vals[i]);
    return d;
}

static void *d_CFDictionaryCreateMutable(void *al, long cap, const void *kcb, const void *vcb)
{
    (void)al;
    (void)cap;
    (void)kcb;
    (void)vcb;
    return cf_new(T_DICT, sizeof(struct cfdict));
}

static void *d_CFDictionaryCreateMutableCopy(void *al, long cap, void *src)
{
    (void)al;
    (void)cap;
    struct cfdict *d = cf_new(T_DICT, sizeof *d), *s = src;
    for (long i = 0; d && type_of(src) == T_DICT && i < s->n; i++)
        dict_set(d, s->k[i], s->v[i]);
    return d;
}

static void *d_CFDictionaryGetValue(void *d, void *k)
{
    struct cfdict *x = d;
    if (type_of(d) != T_DICT)
        return NULL;
    for (long i = 0; i < x->n; i++)
        if (cf_equal(x->k[i], k))
            return x->v[i];
    return NULL;
}

static long d_CFDictionaryGetCount(void *d) { return type_of(d) == T_DICT ? ((struct cfdict *)d)->n : 0; }
static void d_CFDictionarySetValue(void *d, void *k, void *v) { if (type_of(d) == T_DICT) dict_set(d, k, v); }

DSYM("_CFDictionaryCreate", d_CFDictionaryCreate);
DSYM("_CFDictionaryCreateMutable", d_CFDictionaryCreateMutable);
DSYM("_CFDictionaryCreateMutableCopy", d_CFDictionaryCreateMutableCopy);
DSYM("_CFDictionaryGetValue", d_CFDictionaryGetValue);
DSYM("_CFDictionaryGetCount", d_CFDictionaryGetCount);
DSYM("_CFDictionarySetValue", d_CFDictionarySetValue);
DSYM("_CFDictionaryAddValue", d_CFDictionarySetValue);

static void *d_CFNumberCreate(void *al, long type, const void *p)
{
    (void)al;
    struct cfnum *n = cf_new(T_NUMBER, sizeof *n);
    if (!n)
        return NULL;
    switch (type) {
    case 1: case 7: n->i = *(const int8_t *)p; break;
    case 2: case 8: n->i = *(const int16_t *)p; break;
    case 3: case 9: n->i = *(const int32_t *)p; break;
    case 5: case 6: case 12: n->is_float = 1; n->d = type == 5 || type == 12 ? *(const float *)p : *(const double *)p; break;
    case 13: n->is_float = 1; n->d = *(const double *)p; break;
    default: n->i = *(const int64_t *)p; break;
    }
    if (!n->is_float)
        n->d = (double)n->i;
    else
        n->i = (int64_t)n->d;
    return n;
}

static int d_CFNumberGetValue(void *o, long type, void *p)
{
    struct cfnum *n = o;
    if (type_of(o) != T_NUMBER)
        return 0;
    switch (type) {
    case 1: case 7: *(int8_t *)p = (int8_t)n->i; break;
    case 2: case 8: *(int16_t *)p = (int16_t)n->i; break;
    case 3: case 9: *(int32_t *)p = (int32_t)n->i; break;
    case 5: case 12: *(float *)p = (float)n->d; break;
    case 6: case 13: *(double *)p = n->d; break;
    default: *(int64_t *)p = n->i; break;
    }
    return 1;
}

static int d_CFBooleanGetValue(void *b) { return b == &g_true; }

DSYM("_CFNumberCreate", d_CFNumberCreate);
DSYM("_CFNumberGetValue", d_CFNumberGetValue);
DSYM("_CFBooleanGetValue", d_CFBooleanGetValue);

static void *d_CFDateCreate(void *al, double t)
{
    (void)al;
    struct cfdate *d = cf_new(T_DATE, sizeof *d);
    if (d)
        d->t = t;
    return d;
}

static double d_CFAbsoluteTimeGetCurrent(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (double)ts.tv_sec - 978307200.0 + ts.tv_nsec / 1e9;
}

DSYM("_CFDateCreate", d_CFDateCreate);
DSYM("_CFAbsoluteTimeGetCurrent", d_CFAbsoluteTimeGetCurrent);

static void *err_new(long code, const char *msg)
{
    struct cferr *e = cf_new(T_ERROR, sizeof *e);
    if (!e)
        return NULL;
    e->code = code;
    e->desc = str_new(msg, (long)strlen(msg));
    return e;
}

static long d_CFErrorGetCode(void *e) { return type_of(e) == T_ERROR ? ((struct cferr *)e)->code : 0; }
static void *d_CFErrorCopyDescription(void *e) { return type_of(e) == T_ERROR ? d_CFRetain(((struct cferr *)e)->desc) : NULL; }

DSYM("_CFErrorGetCode", d_CFErrorGetCode);
DSYM("_CFErrorCopyDescription", d_CFErrorCopyDescription);

static struct cfstr g_locale_id = { { &g_cls[T_STRING], -1 }, "en_US", 5 };
static struct cfhdr g_locale = { &g_cls[T_LOCALE], -1 };
static struct cfstr g_klocale_key = { { &g_cls[T_STRING], -1 }, "kCFLocaleIdentifierKey", 22 };
static void *d_kCFLocaleIdentifier = &g_klocale_key;
DSYM("_kCFLocaleIdentifier", &d_kCFLocaleIdentifier);

static void *d_CFLocaleCopyCurrent(void) { return &g_locale; }
static void *d_CFLocaleGetValue(void *l, void *k) { (void)l; return k == d_kCFLocaleIdentifier ? &g_locale_id : NULL; }
DSYM("_CFLocaleCopyCurrent", d_CFLocaleCopyCurrent);
DSYM("_CFLocaleGetValue", d_CFLocaleGetValue);

#define ERR_SEC_PARAM          (-50)
#define ERR_SEC_EXPIRED        (-67818)
#define ERR_SEC_HOSTNAME       (-67602)
#define ERR_SEC_NOT_TRUSTED    (-67843)

static void *d_SecCertificateCreateWithData(void *al, void *data)
{
    (void)al;
    if (type_of(data) != T_DATA)
        return NULL;
    struct cfdata *d = data;
    x509_cert c;
    if (x509_parse(d->ptr, (size_t)d->len, &c) != 0)
        return NULL;
    struct seccert *s = cf_new(T_CERT, sizeof *s);
    if (!s)
        return NULL;
    s->der = malloc((size_t)d->len);
    memcpy(s->der, d->ptr, (size_t)d->len);
    s->len = d->len;
    return s;
}

static void *d_SecCertificateCopyData(void *c)
{
    return type_of(c) == T_CERT ? data_new(((struct seccert *)c)->der, ((struct seccert *)c)->len) : NULL;
}

static void *d_SecPolicyCreateSSL(int server, void *host)
{
    (void)server;
    struct secpolicy *p = cf_new(T_POLICY, sizeof *p);
    if (p && host) {
        long n;
        const char *h = str_ptr(host, &n);
        snprintf(p->host, sizeof p->host, "%.*s", (int)n, h);
    }
    return p;
}

static int d_SecTrustCreateWithCertificates(void *certs, void *policies, void **out)
{
    struct sectrust *t = cf_new(T_TRUST, sizeof *t);
    if (!t)
        return ERR_SEC_PARAM;
    t->certs = array_new(1);
    if (type_of(certs) == T_ARRAY) {
        for (long i = 0; i < ((struct cfarray *)certs)->n; i++)
            array_push(t->certs, ((struct cfarray *)certs)->v[i]);
    } else if (type_of(certs) == T_CERT) {
        array_push(t->certs, certs);
    }
    void *pol = type_of(policies) == T_ARRAY ? d_CFArrayGetValueAtIndex(policies, 0) : policies;
    if (type_of(pol) == T_POLICY)
        snprintf(t->host, sizeof t->host, "%s", ((struct secpolicy *)pol)->host);
    *out = t;
    return 0;
}

static int d_SecTrustSetVerifyDate(void *t, void *date)
{
    if (type_of(t) != T_TRUST || type_of(date) != T_DATE)
        return ERR_SEC_PARAM;
    ((struct sectrust *)t)->when = (int64_t)(((struct cfdate *)date)->t + 978307200.0);
    return 0;
}

static pthread_once_t g_store_once = PTHREAD_ONCE_INIT;
static x509_store *g_store;

static void load_store(void)
{
    int fd = open("/etc/ssl/cert.pem", O_RDONLY);
    if (fd < 0)
        return;
    size_t cap = 1 << 20, len = 0;
    uint8_t *buf = malloc(cap);
    while (buf && len < cap) {
        ssize_t r = read(fd, buf + len, cap - len);
        if (r <= 0)
            break;
        len += (size_t)r;
    }
    close(fd);
    g_store = malloc(sizeof *g_store);
    if (g_store && buf)
        x509_store_load_pem(g_store, buf, len);
}

static int d_SecTrustEvaluateWithError(void *tr, void **error)
{
    struct sectrust *t = tr;
    if (error)
        *error = NULL;
    if (type_of(tr) != T_TRUST || !t->certs->n) {
        if (error)
            *error = err_new(ERR_SEC_PARAM, "invalid trust object");
        return 0;
    }
    pthread_once(&g_store_once, load_store);
    long n = t->certs->n;
    x509_cert *chain = calloc((size_t)n, sizeof *chain);
    for (long i = 0; i < n; i++) {
        struct seccert *c = t->certs->v[i];
        if (x509_parse(c->der, (size_t)c->len, &chain[i]) != 0) {
            free(chain);
            if (error)
                *error = err_new(ERR_SEC_PARAM, "unparseable certificate");
            return 0;
        }
    }
    char why[160] = "";
    int64_t now = t->when ? t->when : (int64_t)time(NULL);
    int rc = g_store ? x509_verify_chain(chain, (int)n, g_store, t->host[0] ? t->host : NULL, now, why, sizeof why) : -1;
    free(chain);
    if (rc == 0)
        return 1;
    long code = ERR_SEC_NOT_TRUSTED;
    if (strstr(why, "expired"))
        code = ERR_SEC_EXPIRED;
    else if (strstr(why, "hostname"))
        code = ERR_SEC_HOSTNAME;
    if (error)
        *error = err_new(code, why[0] ? why : "certificate is not trusted");
    return 0;
}

static void *d_SecTrustCopyCertificateChain(void *tr)
{
    if (type_of(tr) != T_TRUST)
        return NULL;
    struct cfarray *src = ((struct sectrust *)tr)->certs, *a = array_new(1);
    for (long i = 0; a && i < src->n; i++)
        array_push(a, src->v[i]);
    return a;
}

static long d_SecTrustGetCertificateCount(void *tr) { return type_of(tr) == T_TRUST ? ((struct sectrust *)tr)->certs->n : 0; }
static void *d_SecTrustGetCertificateAtIndex(void *tr, long i) { return type_of(tr) == T_TRUST ? d_CFArrayGetValueAtIndex(((struct sectrust *)tr)->certs, i) : NULL; }

DSYM("_SecCertificateCreateWithData", d_SecCertificateCreateWithData);
DSYM("_SecCertificateCopyData", d_SecCertificateCopyData);
DSYM("_SecPolicyCreateSSL", d_SecPolicyCreateSSL);
DSYM("_SecTrustCreateWithCertificates", d_SecTrustCreateWithCertificates);
DSYM("_SecTrustSetVerifyDate", d_SecTrustSetVerifyDate);
DSYM("_SecTrustEvaluateWithError", d_SecTrustEvaluateWithError);
DSYM("_SecTrustCopyCertificateChain", d_SecTrustCopyCertificateChain);
DSYM("_SecTrustGetCertificateCount", d_SecTrustGetCertificateCount);
DSYM("_SecTrustGetCertificateAtIndex", d_SecTrustGetCertificateAtIndex);

void *d_cf_opaque_new(size_t size, void (*dtor)(void *))
{
    struct cfopaque *o = cf_new(T_OPAQUE, size < sizeof(struct cfopaque) ? sizeof(struct cfopaque) : size);
    if (o)
        o->dtor = dtor;
    return o;
}

void *d_sec_trust_from_der(const uint8_t *const *der, const size_t *lens, int n, const char *host)
{
    struct sectrust *t = cf_new(T_TRUST, sizeof *t);
    if (!t)
        return NULL;
    t->certs = array_new(1);
    for (int i = 0; i < n; i++) {
        void *d = data_new(der[i], (long)lens[i]);
        void *c = d_SecCertificateCreateWithData(NULL, d);
        d_CFRelease(d);
        if (c) {
            array_push(t->certs, c);
            d_CFRelease(c);
        }
    }
    if (host)
        snprintf(t->host, sizeof t->host, "%s", host);
    return t;
}

static int d_SecTrustEvaluate(void *tr, uint32_t *result)
{
    if (type_of(tr) != T_TRUST)
        return ERR_SEC_PARAM;
    int ok = d_SecTrustEvaluateWithError(tr, NULL);
    if (result)
        *result = ok ? 4 : 5;
    return 0;
}

static int d_SecTrustGetTrustResult(void *tr, uint32_t *result) { return d_SecTrustEvaluate(tr, result); }
static int d_SecTrustSetAnchorCertificates(void *tr, void *a) { (void)tr; (void)a; return 0; }
static int d_SecTrustSetAnchorCertificatesOnly(void *tr, int b) { (void)tr; (void)b; return 0; }

static void *d_SecCopyErrorMessageString(int status, void *reserved)
{
    (void)reserved;
    char b[64];
    snprintf(b, sizeof b, "Security error %d", status);
    return str_new(b, (long)strlen(b));
}

DSYM("_SecTrustEvaluate", d_SecTrustEvaluate);
DSYM("_SecTrustGetTrustResult", d_SecTrustGetTrustResult);
DSYM("_SecTrustSetAnchorCertificates", d_SecTrustSetAnchorCertificates);
DSYM("_SecTrustSetAnchorCertificatesOnly", d_SecTrustSetAnchorCertificatesOnly);
DSYM("_SecCopyErrorMessageString", d_SecCopyErrorMessageString);

#define PREF_KEY(var, text) \
    static struct cfstr var##_s = { { &g_cls[T_STRING], -1 }, (char *)text, sizeof text - 1 }; \
    static void *var = &var##_s;                                                                 \
    DSYM("_" #var, &var)

PREF_KEY(kCFPreferencesCurrentApplication, "kCFPreferencesCurrentApplication");
PREF_KEY(kCFPreferencesAnyApplication, "kCFPreferencesAnyApplication");
PREF_KEY(kCFPreferencesCurrentUser, "kCFPreferencesCurrentUser");
PREF_KEY(kCFPreferencesAnyUser, "kCFPreferencesAnyUser");
PREF_KEY(kCFPreferencesCurrentHost, "kCFPreferencesCurrentHost");
PREF_KEY(kCFPreferencesAnyHost, "kCFPreferencesAnyHost");

static void *d_CFPreferencesCopyAppValue(void *key, void *app) { (void)key; (void)app; return NULL; }
static void *d_CFPreferencesCopyValue(void *key, void *app, void *user, void *host) { (void)key; (void)app; (void)user; (void)host; return NULL; }
static void d_CFPreferencesSetAppValue(void *key, void *v, void *app) { (void)key; (void)v; (void)app; }
static int d_CFPreferencesAppSynchronize(void *app) { (void)app; return 1; }

static int d_CFPreferencesGetAppBooleanValue(void *key, void *app, uint8_t *valid)
{
    (void)key;
    (void)app;
    if (valid)
        *valid = 0;
    return 0;
}

static long d_CFPreferencesGetAppIntegerValue(void *key, void *app, uint8_t *valid)
{
    (void)key;
    (void)app;
    if (valid)
        *valid = 0;
    return 0;
}

DSYM("_CFPreferencesCopyAppValue", d_CFPreferencesCopyAppValue);
DSYM("_CFPreferencesCopyValue", d_CFPreferencesCopyValue);
DSYM("_CFPreferencesSetAppValue", d_CFPreferencesSetAppValue);
DSYM("_CFPreferencesAppSynchronize", d_CFPreferencesAppSynchronize);
DSYM("_CFPreferencesGetAppBooleanValue", d_CFPreferencesGetAppBooleanValue);
DSYM("_CFPreferencesGetAppIntegerValue", d_CFPreferencesGetAppIntegerValue);

static void *tz_new(const char *name)
{
    struct cftz *t = cf_new(T_TZ, sizeof *t);
    if (t)
        t->name = str_new(name, (long)strlen(name));
    return t;
}

static void *d_CFTimeZoneCopySystem(void)
{
    const char *tz = getenv("TZ");
    char link[512];
    if (tz && *tz) {
        if (*tz == ':')
            tz++;
        return tz_new(tz);
    }
    ssize_t n = readlink("/etc/localtime", link, sizeof link - 1);
    if (n > 0) {
        link[n] = 0;
        const char *z = strstr(link, "zoneinfo/");
        if (z)
            return tz_new(z + 9);
    }
    return tz_new("UTC");
}

static void *d_CFTimeZoneCreateWithName(void *al, void *name, int abbrev)
{
    (void)al;
    (void)abbrev;
    long n;
    const char *p = str_ptr(name, &n);
    struct cftz *t = cf_new(T_TZ, sizeof *t);
    if (t)
        t->name = str_new(p, n);
    return t;
}

static void *d_CFTimeZoneGetName(void *tz) { return type_of(tz) == T_TZ ? ((struct cftz *)tz)->name : NULL; }
static void d_CFTimeZoneResetSystem(void) { tzset(); }

static double d_CFTimeZoneGetSecondsFromGMT(void *tz, double at)
{
    (void)tz;
    time_t t = (time_t)(at + 978307200.0);
    struct tm lt;
    localtime_r(&t, &lt);
    return (double)(timegm(&lt) - t);
}

DSYM("_CFTimeZoneCopySystem", d_CFTimeZoneCopySystem);
DSYM("_CFTimeZoneCopyDefault", d_CFTimeZoneCopySystem);
DSYM("_CFTimeZoneCreateWithName", d_CFTimeZoneCreateWithName);
DSYM("_CFTimeZoneGetName", d_CFTimeZoneGetName);
DSYM("_CFTimeZoneResetSystem", d_CFTimeZoneResetSystem);
DSYM("_CFTimeZoneGetSecondsFromGMT", d_CFTimeZoneGetSecondsFromGMT);

static long put_u16(uint8_t *buf, long max, long used, uint32_t u, int be)
{
    if (buf) {
        if (used + 2 > max)
            return -1;
        buf[used + (be ? 0 : 1)] = (uint8_t)(u >> 8);
        buf[used + (be ? 1 : 0)] = (uint8_t)u;
    }
    return used + 2;
}

static long d_CFStringGetBytes(void *s, long loc, long len, uint32_t enc, uint8_t loss, int ext, uint8_t *buf,
                               long max, long *used)
{
    (void)loss;
    long n;
    const char *p = str_ptr(s, &n);
    if (loc < 0 || loc > n)
        loc = n;
    if (len < 0 || loc + len > n)
        len = n - loc;
    const uint8_t *q = (const uint8_t *)p + loc, *e = q + len;
    long u = 0, chars = 0;
    int utf16 = enc == 0x100 || enc == 0x10000100 || enc == 0x14000100;
    int be = enc == 0x10000100 || (enc == 0x100 && ext);
    if (utf16 && ext && buf) {
        if (max < 2)
            goto done;
        buf[0] = be ? 0xfe : 0xff;
        buf[1] = be ? 0xff : 0xfe;
        u = 2;
    }
    while (q < e) {
        if (!utf16) {
            if (buf && u >= max)
                break;
            if (buf)
                buf[u] = *q;
            u++;
            q++;
            chars++;
            continue;
        }
        uint32_t c = *q;
        int k = c < 0x80 ? 1 : c < 0xe0 ? 2 : c < 0xf0 ? 3 : 4;
        if (q + k > e)
            break;
        if (k > 1) {
            c &= 0x7f >> k;
            for (int i = 1; i < k; i++)
                c = (c << 6) | (q[i] & 0x3f);
        }
        long nu;
        if (c >= 0x10000) {
            nu = put_u16(buf, max, u, 0xd800 | ((c - 0x10000) >> 10), be);
            if (nu >= 0)
                nu = put_u16(buf, max, nu, 0xdc00 | ((c - 0x10000) & 0x3ff), be);
        } else {
            nu = put_u16(buf, max, u, c, be);
        }
        if (nu < 0)
            break;
        u = nu;
        q += k;
        chars += k;
    }
done:
    if (used)
        *used = u;
    return chars;
}

DSYM("_CFStringGetBytes", d_CFStringGetBytes);

static void *d_CFLocaleCopyPreferredLanguages(void)
{
    const char *l = getenv("LC_ALL");
    if (!l || !*l)
        l = getenv("LANG");
    char b[32] = "en-US";
    if (l && *l && strcmp(l, "C") && strcmp(l, "POSIX")) {
        size_t n = strcspn(l, ".@");
        if (n >= sizeof b)
            n = sizeof b - 1;
        memcpy(b, l, n);
        b[n] = 0;
        for (char *c = b; *c; c++)
            if (*c == '_')
                *c = '-';
    }
    struct cfarray *a = array_new(1);
    void *s = str_new(b, (long)strlen(b));
    array_push(a, s);
    d_CFRelease(s);
    return a;
}

static int d_LocaleRefGetPartString(void *ref, uint32_t mask, unsigned long max, char *out)
{
    (void)ref;
    (void)mask;
    (void)max;
    (void)out;
    return -50;
}

DSYM("_CFLocaleCopyPreferredLanguages", d_CFLocaleCopyPreferredLanguages);
DSYM("_LocaleRefGetPartString", d_LocaleRefGetPartString);

static void *d_FSEventStreamCreate(void) { return NULL; }
static int d_FSEventStreamStart(void *s) { (void)s; return 0; }
static void d_FSEventStreamNoop(void *s) { (void)s; }
static void d_FSEventStreamSetDispatchQueue(void *s, void *q) { (void)s; (void)q; }

DSYM("_FSEventStreamCreate", d_FSEventStreamCreate);
DSYM("_FSEventStreamStart", d_FSEventStreamStart);
DSYM("_FSEventStreamStop", d_FSEventStreamNoop);
DSYM("_FSEventStreamInvalidate", d_FSEventStreamNoop);
DSYM("_FSEventStreamRelease", d_FSEventStreamNoop);
DSYM("_FSEventStreamSetDispatchQueue", d_FSEventStreamSetDispatchQueue);
DSYM("_FSEventStreamScheduleWithRunLoop", d_FSEventStreamSetDispatchQueue);
