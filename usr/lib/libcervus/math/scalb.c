#define _GNU_SOURCE
#include <math.h>

double scalb(double x, double fn)
{
	if (isnan(x) || isnan(fn))
		return x*fn;
	if (!isfinite(fn)) {
		if (fn > 0.0)
			return x*fn;
		else
			return x/(-fn);
	}
	if (rint(fn) != fn) return (fn-fn)/(fn-fn);
	if ( fn > 65000.0) return scalbn(x, 65000);
	if (-fn > 65000.0) return scalbn(x,-65000);
	return scalbn(x,(int)fn);
}
