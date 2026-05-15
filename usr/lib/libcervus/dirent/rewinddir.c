#include <unistd.h>
#include <libcervus.h>

void rewinddir(DIR *dirp)
{
    if (!dirp) return;
    lseek(dirp->fd, 0, SEEK_SET);
}
