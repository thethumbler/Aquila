#ifndef _SYS_BINFMT_H
#define _SYS_BINFMT_H

#include <core/system.h>
#include <fs/vfs.h>
#include <sys/proc.h>

/** A format of executable files: says whether a file is one, and loads
 * it into a process (sys/binfmt/). */
struct binfmt {
    int (*check)(struct vnode *vnode);
    int (*load)(struct proc *proc, const char *path, struct vnode *vnode);
};

/* Loads a program into a process, whatever its format. */
int binfmt_load(struct proc *proc, const char *path, struct proc **ref);

/* sys/binfmt/elf.c: ELF executables */
int binfmt_elf_check(struct vnode *vnode);
int binfmt_elf_load(struct proc *proc, const char *path, struct vnode *vnode);

#endif /* ! _SYS_BINFMT_H */
