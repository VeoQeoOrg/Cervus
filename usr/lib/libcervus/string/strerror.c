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
        default:      return "Unknown error";
    }
}
