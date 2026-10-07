#include <resolv.h>

struct __res_state *__res_state()
{
	static struct __res_state res;
	return &res;
}
