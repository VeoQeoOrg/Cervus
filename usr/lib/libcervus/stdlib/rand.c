#include <stdlib.h>

static unsigned long __rand_state = 1;

int rand(void)
{
    __rand_state = __rand_state * 1103515245UL + 12345UL;
    return (int)((__rand_state >> 16) & 0x7FFF);
}

void srand(unsigned int seed) { __rand_state = seed; }
