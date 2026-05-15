#include <sys/utsname.h>
#include <string.h>
#include <errno.h>
#include <libcervus.h>

int uname(struct utsname *buf)
{
    if (!buf) { __cervus_errno = EFAULT; return -1; }
    strncpy(buf->sysname,  "Cervus", _UTSNAME_LENGTH - 1);
    strncpy(buf->nodename, "cervus", _UTSNAME_LENGTH - 1);
    strncpy(buf->release,  "0.0.2",  _UTSNAME_LENGTH - 1);
    strncpy(buf->version,  "#1",     _UTSNAME_LENGTH - 1);
    strncpy(buf->machine,  "x86_64", _UTSNAME_LENGTH - 1);
    buf->sysname[_UTSNAME_LENGTH - 1]  = '\0';
    buf->nodename[_UTSNAME_LENGTH - 1] = '\0';
    buf->release[_UTSNAME_LENGTH - 1]  = '\0';
    buf->version[_UTSNAME_LENGTH - 1]  = '\0';
    buf->machine[_UTSNAME_LENGTH - 1]  = '\0';
    return 0;
}
