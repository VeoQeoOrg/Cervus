#define _GNU_SOURCE
#include <math.h>

float scalbf(float x, float fn)
{
	if (isnan(x) || isnan(fn)) return x*fn;
	if (!isfinite(fn)) {
		if (fn > 0.0f)
			return x*fn;
		else
			return x/(-fn);
	}
	if (rintf(fn) != fn) return (fn-fn)/(fn-fn);
	if ( fn > 65000.0f) return scalbnf(x, 65000);
	if (-fn > 65000.0f) return scalbnf(x,-65000);
	return scalbnf(x,(int)fn);
}
