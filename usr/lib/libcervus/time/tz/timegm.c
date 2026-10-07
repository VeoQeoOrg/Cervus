#define _GNU_SOURCE
#include "time_impl.h"
#include <libcervus.h>
#include <errno.h>

time_t timegm(struct tm *tm)
{
	struct tm new;
	long long t = __tm_to_secs(tm);
	if (__secs_to_tm(t, &new) < 0) {
		__cervus_set_errno(EOVERFLOW);
		return -1;
	}
	*tm = new;
	tm->tm_isdst = 0;
	return t;
}
