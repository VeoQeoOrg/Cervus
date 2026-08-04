#ifndef _SYS_UN_H
#define _SYS_UN_H

#include <stdint.h>

#define UNIX_PATH_MAX 108

struct sockaddr_un {
    uint16_t sun_family;
    char     sun_path[UNIX_PATH_MAX];
};

#endif
