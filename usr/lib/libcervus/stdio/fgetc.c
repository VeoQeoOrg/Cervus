#include <stdio.h>

int fgetc(FILE *s)
{
    unsigned char ch;
    if (fread(&ch, 1, 1, s) != 1) return EOF;
    return (int)ch;
}
