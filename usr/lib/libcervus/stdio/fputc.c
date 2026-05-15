#include <stdio.h>

int fputc(int c, FILE *s)
{
    unsigned char ch = (unsigned char)c;
    if (fwrite(&ch, 1, 1, s) != 1) return EOF;
    return (int)ch;
}
