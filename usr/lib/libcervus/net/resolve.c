#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <string.h>

in_addr_t inet_resolve(const char *name)
{
    in_addr_t direct = inet_addr(name);
    if (direct != 0xffffffffu) return direct;
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(name, NULL, &hints, &res) != 0 || !res) return 0xffffffffu;
    in_addr_t a = ((struct sockaddr_in *)res->ai_addr)->sin_addr.s_addr;
    freeaddrinfo(res);
    return a;
}
