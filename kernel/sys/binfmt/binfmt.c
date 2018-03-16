#include <core/arch.h>
#include <fs/vfs.h>
#include <sys/binfmt.h>
#include <sys/elf.h>

struct {
    int (*check)(struct inode *inode);
    int (*load)(proc_t *proc, struct inode *file);
} binfmt_list[] = {
    {binfmt_elf_check, binfmt_elf_load},
};

#define BINFMT_NR ((sizeof(binfmt_list)/sizeof(binfmt_list[0])))

static int binfmt_fmt_load(proc_t *proc, const char *fn, struct inode *file, int (*load)(proc_t *, struct inode *), proc_t **ref)
{
    int err = 0;

    void *arch_specific_data = NULL;
    int new_proc = !proc;

    if (new_proc) {
        arch_specific_data = arch_binfmt_load();
        proc = proc_new();
    } else {
        pmman.unmap_full(0, proc->heap);
        kfree(proc->name);
    }

    if ((err = load(proc, file)))
        goto error;

    proc->name = strdup(fn);

    if (new_proc) {
        pmman.map(USER_STACK_BASE, USER_STACK_SIZE, URW);
        arch_proc_init(arch_specific_data, proc);
        arch_binfmt_end(arch_specific_data);

        if (ref)
            *ref = proc;
    }

    return 0;

error:
    arch_binfmt_end(arch_specific_data);
    return err;
}

int binfmt_load(proc_t *proc, const char *path, proc_t **ref)
{
    struct vnode v;
    struct inode *file = NULL;
    int err;

    if ((err = vfs.lookup(path, 1, 1, &v)))   /* FIXME */
        return err;

    if ((err = vfs.vget(&v, &file)))
        return err;

    for (size_t i = 0; i < BINFMT_NR; ++i) {
        if (!binfmt_list[i].check(file)) {
            binfmt_fmt_load(proc, path, file, binfmt_list[i].load, ref);
            vfs.close(file);
            return 0;
        }
    }

    vfs.close(file);
    return -ENOEXEC;
}

