#include "time_impl.h"
#include <libcervus.h>
#include <errno.h>
#include <limits.h>

struct tm *__localtime_r(const time_t *restrict t, struct tm *restrict tm)
{

	if (*t < INT_MIN * 31622400LL || *t > INT_MAX * 31622400LL) {
		__cervus_set_errno(EOVERFLOW);
		return 0;
	}
	long gmtoff;
	const char *zone;
	int isdst;
	__secs_to_zone(*t, 0, &isdst, &gmtoff, 0, &zone);
	if (__secs_to_tm((long long)*t + gmtoff, tm) < 0) {
		__cervus_set_errno(EOVERFLOW);
		return 0;
	}
	tm->tm_isdst = isdst;
	return tm;
}

weak_alias(__localtime_r, localtime_r);
