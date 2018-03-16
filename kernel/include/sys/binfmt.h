#ifndef _BINFMT_H
#define _BINFMT_H

#include <sys/proc.h>
int binfmt_load(proc_t *proc, const char *path, proc_t **ref);

#endif
