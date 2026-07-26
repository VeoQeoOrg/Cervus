#ifndef PWUTIL_H
#define PWUTIL_H

#include <stdint.h>

int pw_getpass(const char *prompt, char *buf, int cap);
int pw_lookup_name(const char *name, uint32_t *uid, uint32_t *gid,
                   char *home, int home_cap, char *shell, int shell_cap);
int pw_lookup_uid(uint32_t uid, char *name, int name_cap,
                  char *home, int home_cap, char *shell, int shell_cap);

#endif
