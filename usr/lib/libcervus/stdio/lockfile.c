#include <stdio.h>
#include <libcervus.h>

void flockfile(FILE *stream)
{
    if (stream) __cervus_lock(&stream->lock);
}

void funlockfile(FILE *stream)
{
    if (stream) __cervus_unlock(&stream->lock);
}

int ftrylockfile(FILE *stream)
{
    if (!stream) return -1;
    __cervus_lock(&stream->lock);
    return 0;
}

int putc_unlocked(int c, FILE *stream) { return fputc(c, stream); }
int getc_unlocked(FILE *stream)        { return fgetc(stream); }
int putchar_unlocked(int c)            { return fputc(c, stdout); }
int getchar_unlocked(void)             { return fgetc(stdin); }
