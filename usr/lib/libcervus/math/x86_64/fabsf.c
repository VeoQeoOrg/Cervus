#include <math.h>

float fabsf(float x)
{
	float t;
	__asm__ ("pcmpeqd %0, %0" : "=x"(t));
	__asm__ ("psrld   $1, %0" : "+x"(t));
	__asm__ ("andps   %1, %0" : "+x"(x) : "x"(t));
	return x;
}
