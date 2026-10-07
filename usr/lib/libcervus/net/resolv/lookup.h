#ifndef LOOKUP_H
#define LOOKUP_H

#include <stdint.h>
#include <stddef.h>
#include <netinet/in.h>
#include <netdb.h>

struct aibuf {
	struct addrinfo ai;
	union sa {
		struct sockaddr_in sin;
		struct sockaddr_in6 sin6;
	} sa;
	volatile int lock[1];
	short slot, ref;
};

struct address {
	int family;
	unsigned scopeid;
	uint8_t addr[16];
	int sortkey;
};

struct service {
	uint16_t port;
	unsigned char proto, socktype;
};

#define MAXNS 3

#define hidden __attribute__((__visibility__("hidden")))
#define weak_alias(old, new) extern __typeof(old) new __attribute__((__weak__, __alias__(#old)))
#define LOCK(x) do { while (__atomic_exchange_n((x), 1, __ATOMIC_ACQUIRE)) __builtin_ia32_pause(); } while (0)
#define UNLOCK(x) __atomic_store_n((x), 0, __ATOMIC_RELEASE)

struct resolvconf {
	struct address ns[MAXNS];
	unsigned nns, attempts, ndots;
	unsigned timeout;
};

#define MAXADDRS 48
#define MAXSERVS 2

hidden int __lookup_serv(struct service buf[static MAXSERVS], const char *name, int proto, int socktype, int flags);
hidden int __lookup_name(struct address buf[static MAXADDRS], char canon[static 256], const char *name, int family, int flags);
hidden int __lookup_ipliteral(struct address buf[static 1], const char *name, int family);

hidden int __get_resolv_conf(struct resolvconf *, char *, size_t);
hidden int __res_msend_rc(int, const unsigned char *const *, const int *, unsigned char *const *, int *, int, const struct resolvconf *);

hidden int __dn_expand(const unsigned char *, const unsigned char *, const unsigned char *, char *, int);
hidden int __res_mkquery(int, const char *, int, int, const unsigned char *, int, const unsigned char *, unsigned char *, int);
hidden int __res_send(const unsigned char *, int, unsigned char *, int);
hidden int __res_msend(int, const unsigned char *const *, const int *, unsigned char *const *, int *, int);

hidden int __dns_parse(const unsigned char *, int, int (*)(void *, int, const void *, int, const void *, int), void *);

#endif
