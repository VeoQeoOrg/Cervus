#include <math.h>

double fabs(double x)
{
	double t;
	__asm__ ("pcmpeqd %0, %0" : "=x"(t));
	__asm__ ("psrlq   $1, %0" : "+x"(t));
	__asm__ ("andps   %1, %0" : "+x"(x) : "x"(t));
	return x;
}
