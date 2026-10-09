#ifndef CERVUS_PTIMER_H
#define CERVUS_PTIMER_H

#include <stdint.h>
#include "../signal/signal.h"

struct task;

#define SI_TIMER_CODE (-2)

void ptimer_group_exit(struct task *leader);
void ptimer_signal_taken(struct task *t, ksiginfo_t *si);
void ptimer_signal_dropped(struct task *t, int timerid);

#endif
