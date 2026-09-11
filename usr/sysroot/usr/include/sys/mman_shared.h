#ifndef _SYS_MMAN_SHARED_H
#define _SYS_MMAN_SHARED_H

#define MFD_CLOEXEC 1

int memfd_create(const char *name, unsigned int flags);

#endif
