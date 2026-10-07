#ifndef _NETDB_H
#define _NETDB_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <sys/socket.h>
#include <netinet/in.h>

struct addrinfo {
    int              ai_flags;
    int              ai_family;
    int              ai_socktype;
    int              ai_protocol;
    socklen_t        ai_addrlen;
    struct sockaddr *ai_addr;
    char            *ai_canonname;
    struct addrinfo *ai_next;
};

#define AI_PASSIVE      0x01
#define AI_CANONNAME    0x02
#define AI_NUMERICHOST  0x04
#define AI_V4MAPPED     0x08
#define AI_ALL          0x10
#define AI_ADDRCONFIG   0x20
#define AI_NUMERICSERV  0x400

#define NI_NUMERICHOST  0x01
#define NI_NUMERICSERV  0x02
#define NI_NOFQDN       0x04
#define NI_NAMEREQD     0x08
#define NI_DGRAM        0x10
#define NI_NUMERICSCOPE 0x100

#define NI_MAXHOST      1025
#define NI_MAXSERV      32

#define EAI_BADFLAGS    -1
#define EAI_NONAME      -2
#define EAI_AGAIN       -3
#define EAI_FAIL        -4
#define EAI_NODATA      -5
#define EAI_FAMILY      -6
#define EAI_SOCKTYPE    -7
#define EAI_SERVICE     -8
#define EAI_ADDRFAMILY  -9
#define EAI_MEMORY      -10
#define EAI_SYSTEM      -11
#define EAI_OVERFLOW    -12

int  getaddrinfo(const char *__restrict node, const char *__restrict service,
                 const struct addrinfo *__restrict hints, struct addrinfo **__restrict res);
void freeaddrinfo(struct addrinfo *res);
int  getnameinfo(const struct sockaddr *__restrict sa, socklen_t salen,
                 char *__restrict host, socklen_t hostlen,
                 char *__restrict serv, socklen_t servlen, int flags);
const char *gai_strerror(int errcode);

struct netent {
    char    *n_name;
    char   **n_aliases;
    int      n_addrtype;
    uint32_t n_net;
};

struct hostent {
    char  *h_name;
    char **h_aliases;
    int    h_addrtype;
    int    h_length;
    char **h_addr_list;
};

#define h_addr h_addr_list[0]

struct servent {
    char  *s_name;
    char **s_aliases;
    int    s_port;
    char  *s_proto;
};

struct protoent {
    char  *p_name;
    char **p_aliases;
    int    p_proto;
};

void            sethostent(int stayopen);
void            endhostent(void);
struct hostent *gethostent(void);

void            setnetent(int stayopen);
void            endnetent(void);
struct netent  *getnetent(void);
struct netent  *getnetbyaddr(uint32_t net, int type);
struct netent  *getnetbyname(const char *name);

void            setservent(int stayopen);
void            endservent(void);
struct servent *getservent(void);
struct servent *getservbyname(const char *name, const char *proto);
struct servent *getservbyport(int port, const char *proto);

void             setprotoent(int stayopen);
void             endprotoent(void);
struct protoent *getprotoent(void);
struct protoent *getprotobyname(const char *name);
struct protoent *getprotobynumber(int proto);

struct hostent *gethostbyname(const char *name);
struct hostent *gethostbyaddr(const void *addr, socklen_t len, int type);
struct hostent *gethostbyname2(const char *name, int af);

int *__h_errno_location(void) __attribute__((__const__));
#define h_errno (*__h_errno_location())

#define NETDB_INTERNAL -1
#define NETDB_SUCCESS  0
#define HOST_NOT_FOUND 1
#define TRY_AGAIN      2
#define NO_RECOVERY    3
#define NO_DATA        4
#define NO_ADDRESS     NO_DATA

void        herror(const char *s);
const char *hstrerror(int err);

int gethostbyname_r(const char *name, struct hostent *ret, char *buf, size_t buflen,
                    struct hostent **result, int *h_errnop);
int gethostbyname2_r(const char *name, int af, struct hostent *ret, char *buf, size_t buflen,
                     struct hostent **result, int *h_errnop);
int gethostbyaddr_r(const void *addr, socklen_t len, int type, struct hostent *ret,
                    char *buf, size_t buflen, struct hostent **result, int *h_errnop);
int getservbyport_r(int port, const char *proto, struct servent *ret, char *buf, size_t buflen,
                    struct servent **result);
int getservbyname_r(const char *name, const char *proto, struct servent *ret, char *buf,
                    size_t buflen, struct servent **result);

#ifdef __cplusplus
}
#endif
#endif
