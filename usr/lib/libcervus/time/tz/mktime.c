#include "time_impl.h"
#include <errno.h>
#include <libcervus.h>

time_t mktime(struct tm *tm)
{
	struct tm new;
	long opp, gmtoff;
	const char *zone;
	long long t = __tm_to_secs(tm);

	__secs_to_zone(t, 1, &new.tm_isdst, &gmtoff, &opp, &zone);

	if (tm->tm_isdst>=0 && new.tm_isdst!=tm->tm_isdst)
		t -= opp - gmtoff;

	t -= gmtoff;
	if ((time_t)t != t) goto error;

	__secs_to_zone(t, 0, &new.tm_isdst, &gmtoff, &opp, &zone);

	if (__secs_to_tm(t + gmtoff, &new) < 0) goto error;

	*tm = new;
	return t;

error:
	__cervus_set_errno(EOVERFLOW);
	return -1;
}
