#include <utmp.h>
#include <stddef.h>

void setutent(void) {}
struct utmp *getutent(void) { return NULL; }
struct utmp *getutid(const struct utmp *ut) { (void)ut; return NULL; }
struct utmp *getutline(const struct utmp *ut) { (void)ut; return NULL; }
struct utmp *pututline(const struct utmp *ut) { (void)ut; return NULL; }
void endutent(void) {}
int utmpname(const char *file) { (void)file; return 0; }
