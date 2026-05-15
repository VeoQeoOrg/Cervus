#include <libcervus.h>

int dirfd(DIR *dirp) { return dirp ? dirp->fd : -1; }
