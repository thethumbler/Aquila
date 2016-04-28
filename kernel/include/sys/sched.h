#ifndef _SCHED_H
#define _SCHED_H

#include <core/system.h>
#include <sys/proc.h>

extern proc_t *cur_proc;

void spawn_init(proc_t *init);
void schedule();

#endif /* ! _SCHED_H */