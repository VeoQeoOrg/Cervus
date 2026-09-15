#include <string.h>
#include <errno.h>

char *strerror(int err)
{
    switch (err) {
        case 0:       return "Success";
        case EPERM:   return "Operation not permitted";
        case ENOENT:  return "No such file or directory";
        case ESRCH:   return "No such process";
        case EINTR:   return "Interrupted system call";
        case EIO:     return "Input/output error";
        case EBADF:   return "Bad file descriptor";
        case ECHILD:  return "No child processes";
        case EAGAIN:  return "Resource temporarily unavailable";
        case ENOMEM:  return "Cannot allocate memory";
        case EACCES:  return "Permission denied";
        case EFAULT:  return "Bad address";
        case EBUSY:   return "Device or resource busy";
        case EEXIST:  return "File exists";
        case ENODEV:  return "No such device";
        case ENOTDIR: return "Not a directory";
        case EISDIR:  return "Is a directory";
        case EINVAL:  return "Invalid argument";
        case EMFILE:  return "Too many open files";
        case ENOTTY:  return "Inappropriate ioctl for device";
        case ENOSPC:  return "No space left on device";
        case EPIPE:   return "Broken pipe";
        case ENOSYS:  return "Function not implemented";
        case ENXIO:   return "No such device or address";
        case E2BIG:   return "Argument list too long";
        case ENOEXEC: return "Exec format error";
        case EXDEV:   return "Invalid cross-device link";
        case ENFILE:  return "Too many open files in system";
        case EFBIG:   return "File too large";
        case EROFS:   return "Read-only file system";
        case EMLINK:  return "Too many links";
        case EDOM:    return "Numerical argument out of domain";
        case ERANGE:  return "Numerical result out of range";
        case EDEADLK: return "Resource deadlock avoided";
        case ENAMETOOLONG: return "File name too long";
        case ENOLCK:  return "No locks available";
        case ENOTEMPTY: return "Directory not empty";
        case ELOOP:   return "Too many levels of symbolic links";
        case EOVERFLOW: return "Value too large for defined data type";
        case EPROTO:  return "Protocol error";
        case ENOTSOCK: return "Socket operation on non-socket";
        case EDESTADDRREQ: return "Destination address required";
        case EMSGSIZE: return "Message too long";
        case EPROTOTYPE: return "Protocol wrong type for socket";
        case ENOPROTOOPT: return "Protocol not available";
        case EPROTONOSUPPORT: return "Protocol not supported";
        case EOPNOTSUPP: return "Operation not supported";
        case EAFNOSUPPORT: return "Address family not supported by protocol";
        case EADDRINUSE: return "Address already in use";
        case EADDRNOTAVAIL: return "Cannot assign requested address";
        case ENETDOWN: return "Network is down";
        case ENETUNREACH: return "Network is unreachable";
        case ENETRESET: return "Network dropped connection on reset";
        case ECONNABORTED: return "Software caused connection abort";
        case ECONNRESET: return "Connection reset by peer";
        case ENOBUFS: return "No buffer space available";
        case EISCONN: return "Transport endpoint is already connected";
        case ENOTCONN: return "Transport endpoint is not connected";
        case ETIMEDOUT: return "Connection timed out";
        case ECONNREFUSED: return "Connection refused";
        case EALREADY: return "Operation already in progress";
        case EINPROGRESS: return "Operation now in progress";
        default:      return "Unknown error";
    }
}
