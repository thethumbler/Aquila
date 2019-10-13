#ifndef _SYS_BINFMT_H
#define _SYS_BINFMT_H

#include <core/system.h>
#include <fs/vfs.h>
#include <sys/proc.h>

/**
 * \ingroup sys
 * \brief binary format
 */
struct binfmt {
    int (*check)(struct vnode *vnode);
    int (*load)(struct proc *proc, const char *path, struct vnode *vnode);
};

int binfmt_load(struct proc *proc, const char *path, struct proc **ref);

/* sys/binfmt/elf.c */
int binfmt_elf_check(struct vnode *vnode);
int binfmt_elf_load(struct proc *proc, const char *path, struct vnode *vnode);

#endif /* ! _SYS_BINFMT_H */
