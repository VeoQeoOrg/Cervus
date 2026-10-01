#ifndef _NET_IF_H
#define _NET_IF_H
#ifdef __cplusplus
extern "C" {
#endif

#include <sys/socket.h>

#define IF_NAMESIZE 16
#define IFNAMSIZ    IF_NAMESIZE

#define IFF_UP          0x1
#define IFF_BROADCAST   0x2
#define IFF_DEBUG       0x4
#define IFF_LOOPBACK    0x8
#define IFF_POINTOPOINT 0x10
#define IFF_RUNNING     0x40
#define IFF_NOARP       0x80
#define IFF_PROMISC     0x100
#define IFF_MULTICAST   0x1000

struct if_nameindex {
    unsigned int if_index;
    char        *if_name;
};

unsigned int         if_nametoindex(const char *name);
char                *if_indextoname(unsigned int index, char *name);
struct if_nameindex *if_nameindex(void);
void                 if_freenameindex(struct if_nameindex *ptr);

#ifdef __cplusplus
}
#endif
#endif
