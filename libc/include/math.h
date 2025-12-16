#ifndef _MATH_H
#define _MATH_H

int abs(int n);
int isinf(double x);
int isnan(double x);
double fabs(double x);
double pow10(int n);
double pow(double x, double y);

#define INFINITY (1.0/0.0)
#define NAN (0.0/0.0)

#endif