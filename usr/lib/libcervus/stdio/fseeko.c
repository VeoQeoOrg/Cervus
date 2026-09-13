#include <stdio.h>

int fseeko(FILE *stream, off_t off, int whence)
{
    return fseek(stream, (long)off, whence);
}

off_t ftello(FILE *stream)
{
    return (off_t)ftell(stream);
}
