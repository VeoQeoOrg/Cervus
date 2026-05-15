#include <time.h>

struct tm *localtime(const time_t *t) { return gmtime(t); }
