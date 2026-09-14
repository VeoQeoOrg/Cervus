#include <unistd.h>

uid_t geteuid(void) { return getuid(); }
gid_t getegid(void) { return getgid(); }
