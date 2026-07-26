#ifndef SECURITY_AUTH_H
#define SECURITY_AUTH_H

#include <stdint.h>

int auth_verify(uint32_t uid, const char *password);
int auth_set_password(uint32_t uid, const char *password);
int auth_is_sudoer(uint32_t uid);
int auth_has_any_password(uint32_t uid);

#endif
