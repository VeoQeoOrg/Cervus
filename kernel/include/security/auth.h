#ifndef SECURITY_AUTH_H
#define SECURITY_AUTH_H

#include <stdint.h>

int auth_verify(uint32_t uid, const char *password);
int auth_set_password(uint32_t uid, const char *password);
int auth_is_sudoer(uint32_t uid);
int auth_has_any_password(uint32_t uid);

#define AUTH_SET_GID    (1ULL << 32)
#define AUTH_CLAIM_SEAT (1ULL << 33)

uint32_t seat_owner_uid(void);
int      seat_active_vt(void);
int      seat_may_use(void);
void     seat_task_exit(uint32_t pid);
void     seat_vt_switched(int vt);

#endif
