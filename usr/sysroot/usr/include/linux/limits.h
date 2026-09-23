#ifndef _LINUX_LIMITS_H
#define _LINUX_LIMITS_H

#include <limits.h>

#ifndef NGROUPS_MAX
#define NGROUPS_MAX 65536
#endif
#ifndef ARG_MAX
#define ARG_MAX 131072
#endif
#ifndef XATTR_NAME_MAX
#define XATTR_NAME_MAX 255
#endif
#ifndef XATTR_SIZE_MAX
#define XATTR_SIZE_MAX 65536
#endif
#ifndef PIPE_BUF
#define PIPE_BUF 4096
#endif

#endif
