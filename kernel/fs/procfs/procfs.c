#include <core/system.h>
#include <core/arch.h>
#include <core/module.h>
#include <core/string.h>
#include <core/time.h>
#include <bits/errno.h>
#include <boot/boot.h>

#include <fs/vfs.h>
#include <fs/devfs.h>
#include <fs/posix.h>
#include <fs/vcache.h>

#include <sys/proc.h>
#include <sys/sched.h>

#include <mm/vm.h>
#include <mm/buddy.h>
#include <ds/buddy.h>

struct fs procfs;

struct procfs_entry {
    char *name;
    ssize_t (*read)(off_t off, size_t size, char *buf);
};

/* procfs root directory (usually mounted on /proc) */
struct vnode *procfs_root = NULL;
struct vcache procfs_vcache = {0};

/* proc/meminfo */
static ssize_t procfs_meminfo(off_t off, size_t size, char *buf)
{
    char meminfo_buf[512];
    extern size_t k_total_mem, k_used_mem, kvmem_used, kvmem_obj_cnt;

    int sz = snprintf(meminfo_buf, 512, 
            "MemTotal: %d kB\n"
            "MemFree: %d kB\n"
            "KVMemUsed: %d KB\n"
            "KVMemObjCnt: %d\n",
            k_total_mem/1024,
            (k_total_mem-k_used_mem)/1024,
            kvmem_used/1024,
            kvmem_obj_cnt
            );

    if (off < sz) {
        ssize_t ret = MIN(size, (size_t)(sz - off));
        memcpy(buf, meminfo_buf + off, ret);
        return ret;
    }

    return 0;
}

/* proc/kvmem */
static ssize_t procfs_kvmem(off_t off, size_t size, char *buf)
{
    //printk("procfs_kvmem(off=%d, size=%d, buf=%p)\n", off, size, buf);

    extern struct queue *malloc_types;

    char kvmem_buf[1024];
    int sz = 0;

    queue_for (node, malloc_types) {
        struct malloc_type *type = (struct malloc_type *) node->value;
        sz += snprintf(kvmem_buf + sz, sizeof(kvmem_buf) - sz,
                "%s %d %d\n", type->name, type->nr, type->total) - 1;

        if ((size_t) sz >= sizeof(kvmem_buf))
            break;
    }

    if (off < sz) {
        ssize_t ret = MIN(size, (size_t)(sz - off));
        memcpy(buf, kvmem_buf + off, ret);
        return ret;
    }

    return 0;
}

/* proc/version */
static ssize_t procfs_version(off_t off, size_t size, char *buf)
{
    char version_buf[512];

    int sz = snprintf(version_buf, 512,
            "%s version %s (%s version %s) %s\n",
            UTSNAME_SYSNAME, UTSNAME_RELEASE,
            __CONFIG_COMPILER__,
            __CONFIG_COMPILER_VERSION__,
            __CONFIG_TIMESTAMP__
            );

    if (off < sz) {
        ssize_t ret = MIN(size, (size_t)(sz - off));
        memcpy(buf, version_buf + off, ret);
        return ret;
    }

    return 0;
}

/* proc/uptime */
static ssize_t procfs_uptime(off_t off, size_t size, char *buf)
{
    char uptime_buf[64];

    int sz = snprintf(uptime_buf, 64, "%ld\n", arch_rtime_ms() / 1000);

    if (off < sz) {
        ssize_t ret = MIN(size, (size_t)(sz - off));
        memcpy(buf, uptime_buf + off, ret);
        return ret;
    }

    return 0;
}

/* proc/filesystems */
static ssize_t procfs_filesystems(off_t off, size_t size, char *buf)
{
    char fs_buf[512];

    extern struct fs_list *registered_fs;

    int sz = 0;

    for (struct fs_list *fs = registered_fs; fs; fs = fs->next) {
        sz += snprintf(fs_buf + sz, sizeof(fs_buf) - sz, "%s\t%s\n", fs->fs->nodev? "nodev" : "", fs->name);
        if ((size_t) sz >= sizeof(fs_buf))
            break;
    }

    if (off < sz) {
        ssize_t ret = MIN(size, (size_t)(sz - off));
        memcpy(buf, fs_buf + off, ret);
        return ret;
    }

    return 0;
}

/* proc/cmdline */
static ssize_t procfs_cmdline(off_t off, size_t size, char *buf)
{
    char cmdline_buf[512];

    extern struct boot *__kboot;

    int sz = snprintf(cmdline_buf, 512, "%s\n", __kboot->cmdline);

    if (off < sz) {
        ssize_t ret = MIN(size, (size_t)(sz - off));
        memcpy(buf, cmdline_buf + off, ret);
        return ret;
    }

    return 0;
}

/* proc/rdcmdline */
static ssize_t procfs_rdcmdline(off_t off, size_t size, char *buf)
{
    char rdcmdline_buf[512];

    extern struct boot *__kboot;
    module_t *rd_module = &__kboot->modules[0];

    int sz = snprintf(rdcmdline_buf, 512, "%s\n", rd_module->cmdline);

    if (off < sz) {
        ssize_t ret = MIN(size, (size_t)(sz - off));
        memcpy(buf, rdcmdline_buf + off, ret);
        return ret;
    }

    return 0;
}

/* proc/mounts */
static ssize_t procfs_mounts(off_t off, size_t size, char *buf)
{
    char mounts_buf[4096];

    extern struct queue *mounts;
    int sz = 0;

    queue_for (node, mounts) {
        struct mountpoint *mp = node->value;
        sz += snprintf(mounts_buf + sz, sizeof(mounts_buf) - sz,
                "%s %s %s %s\n", mp->dev, mp->path, mp->type, mp->options);
        if ((size_t) sz >= sizeof(mounts_buf))
            break;
    }


    if (off < sz) {
        ssize_t ret = MIN(size, (size_t)(sz - off));
        memcpy(buf, mounts_buf + off, ret);
        return ret;
    }

    return 0;
}

/* proc/buddyinfo */
static ssize_t procfs_buddyinfo(off_t off, size_t size, char *buf)
{
    char buddyinfo_buf[4096];

    extern struct buddy buddies[BUDDY_ZONE_NR][BUDDY_MAX_ORDER+1];

    int sz = 0;

    /*
    sz += snprintf(buddyinfo_buf + sz, sizeof(buddyinfo_buf) - sz, "Zone DMA\n");
    for (int i = 0; i < BUDDY_MAX_ORDER + 1; ++i) {
        sz += snprintf(buddyinfo_buf + sz, sizeof(buddyinfo_buf) - sz, "2^%d: %d\n",
                12 + i, buddies[0][i].usable);
    }
    */

    sz += snprintf(buddyinfo_buf + sz, sizeof(buddyinfo_buf) - sz, "\nZone Normal\n");
    for (int i = 0; i < BUDDY_MAX_ORDER + 1; ++i) {
        sz += snprintf(buddyinfo_buf + sz, sizeof(buddyinfo_buf) - sz, "2^%d: %d\n",
                12 + i, buddies[1][i].usable);
    }

    sz += snprintf(buddyinfo_buf + sz, sizeof(buddyinfo_buf) - sz, "\n");


    if (off < sz) {
        ssize_t ret = MIN(size, (size_t)(sz - off));
        memcpy(buf, buddyinfo_buf + off, ret);
        return ret;
    }

    return 0;
}

/* proc/devices */
static ssize_t procfs_devices(off_t off, size_t size, char *buf)
{
    char dev_buf[512];

    int sz = 0;

    extern struct dev *chrdev[256];
    extern struct dev *blkdev[256];

    sz += snprintf(dev_buf + sz, sizeof(dev_buf) - sz, "chrdev:\n");
    for (int i = 0; i < 256; ++i) {
        if (chrdev[i]) {
            sz += snprintf(dev_buf + sz, sizeof(dev_buf) - sz, "%d\t%s\n", i, chrdev[i]->name);
            if ((size_t) sz >= sizeof(dev_buf))
                break;
        }
    }

    sz += snprintf(dev_buf + sz, sizeof(dev_buf) - sz, "\nblkdev:\n");
    for (int i = 0; i < 256; ++i) {
        if (blkdev[i]) {
            sz += snprintf(dev_buf + sz, sizeof(dev_buf) - sz, "%d\t%s\n", i, blkdev[i]->name);
            if ((size_t) sz >= sizeof(dev_buf))
                break;
        }
    }

    if (off < sz) {
        ssize_t ret = MIN(size, (size_t)(sz - off));
        memcpy(buf, dev_buf + off, ret);
        return ret;
    }

    return 0;
}

static struct procfs_entry entries[] = {
    {"meminfo", procfs_meminfo},
    {"cmdline", procfs_cmdline},
    {"rdcmdline", procfs_rdcmdline},
    {"version", procfs_version},
    {"uptime",  procfs_uptime},
    {"filesystems",  procfs_filesystems},
    //{"zoneinfo",  procfs_zoneinfo},
    {"buddyinfo", procfs_buddyinfo},
    {"kvmem", procfs_kvmem},
    {"mounts", procfs_mounts},
    {"devices", procfs_devices},
};

#define PROCFS_ENTRIES  (sizeof(entries)/sizeof(entries[0]))

static ssize_t procfs_proc_status(int pid, off_t off, size_t size, void *buf)
{
    struct proc *proc = proc_pid_find(pid);

    if (!proc)
        return -ENONET;

    char status_buf[512];

    int sz = snprintf(status_buf, 512,
            "name: %s\n"
            "pid: %d\n"
            "ppid: %d\n"
            "cwd: %s\n"
            "umask: %x\n"
            "uid: %d\n"
            "gid: %d\n"
            "heap: 0x%x\n"
            "threads_nr: %d\n",
            proc->name,
            proc->pid,
            proc->parent ? proc->parent->pid : 0,
            proc->cwd,
            proc->mask,
            proc->uid,
            proc->gid,
            proc->heap,
            proc->threads.count
            );

    if (off < sz) {
        ssize_t ret = MIN(size, (size_t)(sz - off));
        memcpy(buf, status_buf + off, ret);
        return ret;
    }

    return 0;
}

static ssize_t procfs_proc_maps(int pid, off_t off, size_t size, void *buf)
{
    struct proc *proc = proc_pid_find(pid);

    if (!proc)
        return -ENONET;

    char maps_buf[4096];

    int sz = 0;

    struct queue *vm_entries = &proc->vm_space.vm_entries;
    queue_for (node, vm_entries) {
        struct vm_entry *vm_entry = node->value;

        char perm[5] = {0};
        perm[0] = vm_entry->flags & VM_UR? 'r' : '-';
        perm[1] = vm_entry->flags & VM_UW? 'w' : '-';
        perm[2] = vm_entry->flags & VM_UX? 'x' : '-';
        perm[3] = vm_entry->flags & VM_SHARED? 's' : 'p';

        char *desc = "";

        if (vm_entry == proc->stack_vm)
            desc = "[stack]";

        if (vm_entry == proc->heap_vm)
            desc = "[heap]";

        struct vnode *vnode = NULL;

        if (vm_entry->vm_object && vm_entry->vm_object->type == VMOBJ_FILE)
            vnode = (struct vnode *) vm_entry->vm_object->p;

        sz += snprintf(maps_buf + sz, sizeof(maps_buf) - sz,
                "%x-%x %s %x %x %x %s\n",
                vm_entry->base,  /* Start address */
                vm_entry->base + vm_entry->size,  /* End address */
                perm,   /* Access permissions */
                vm_entry->off, /* Offset in file */
                vnode? vnode->dev : 0, /* Device ID */
                vnode? vnode->ino : 0, /* Inode ID */
                desc); 
    }
    
    if (off < sz) {
        ssize_t ret = MIN(size, (size_t)(sz - off));
        memcpy(buf, maps_buf + off, ret);
        return ret;
    }

    return 0;
}

static ssize_t procfs_proc_vmstats(int pid, off_t off, size_t size, void *buf)
{
    struct proc *proc = proc_pid_find(pid);

    if (!proc)
        return -ENONET;

    char vmstats_buf[4096];

    int sz = 0;

    struct queue *vm_entries = &proc->vm_space.vm_entries;

    queue_for (node, vm_entries) {
        struct vm_entry *vm_entry = node->value;

        char perm[5] = {0};
        perm[0] = vm_entry->flags & VM_UR? 'r' : '-';
        perm[1] = vm_entry->flags & VM_UW? 'w' : '-';
        perm[2] = vm_entry->flags & VM_UX? 'x' : '-';
        perm[3] = vm_entry->flags & VM_SHARED? 's' : 'p';

        const char *type = "";
        switch (vm_entry->vm_object->type) {
            case VMOBJ_FILE: type = "file"; break;
            case VMOBJ_ZERO: type = "anon"; break;
            //case VMOBJ_SHADOW: type = "shadow"; break;
        }

        /*
        printk("%p-%p %s %p %p %s\n",
                vm_entry->base,
                vm_entry->base + vm_entry->size,
                perm,
                vm_entry->off,
                vm_entry->vm_object,
                type); 
                */

        sz += snprintf(vmstats_buf + sz, sizeof(vmstats_buf) - sz,
                "%p-%p %s %p %p %s\n",
                vm_entry->base,
                vm_entry->base + vm_entry->size,
                perm,
                vm_entry->off,
                vm_entry->vm_object,
                0); //type); 
    }
    
    if (off < sz) {
        ssize_t ret = MIN(size, (size_t)(sz - off));
        memcpy(buf, vmstats_buf + off, ret);
        return ret;
    }

    return 0;
}

struct procfs_proc_entry {
    char *name;
    ssize_t (*read)(int pid, off_t off, size_t size, void *buf);
};

static struct procfs_proc_entry proc_entries[] = {
    {"status", procfs_proc_status},
    {"maps", procfs_proc_maps},
    {"vmstats", procfs_proc_vmstats},
};

#define PROCFS_PROC_ENTRIES  (sizeof(proc_entries)/sizeof(proc_entries[0]))

static ssize_t procfs_read(struct vnode *node, off_t offset, size_t size, void *buf)
{
    size_t id = (size_t) node->p;

    if ((ssize_t) node->ino < 0) {
        if (id < PROCFS_ENTRIES)
            return entries[id].read(offset, size, buf);
    } else {
        size_t pid = node->ino / (PROCFS_PROC_ENTRIES + 1);
        id -= 1;
        if (id < PROCFS_PROC_ENTRIES)
            return proc_entries[id].read(pid, offset, size, buf);
    }

    return -ENOSYS;
}

static ssize_t procfs_readdir(struct vnode *vnode, off_t offset, struct dirent *dirent)
{
    //printk("procfs_readdir(vnode=%p, offset=%d, dirent=%p)\n", vnode, offset, dirent);

    if (vnode == procfs_root) {
        if (offset == 0) {
            dirent->d_ino = 0;
            strcpy(dirent->d_name, ".");
            return 1;
        } else if (offset == 1) {
            dirent->d_ino = 1;
            strcpy(dirent->d_name, "..");
            return 1;
        }

        offset -= 2;

        if ((size_t) offset < PROCFS_ENTRIES) {
            //dirent->d_ino = entries[offset].id;
            strcpy(dirent->d_name, entries[offset].name);
            return 1;
        }

        /* Processes go here */
        offset -= PROCFS_ENTRIES;
        
        if (!offset) {
            snprintf(dirent->d_name, 10, "self");
            return 1;
        }

        --offset;

        queue_for (node, procs) {
            if (!offset) {
                struct proc *proc = node->value;
                snprintf(dirent->d_name, 10, "%d", proc->pid);
                return 1;
            }

            --offset;
        }
    } else if (vnode->ino > 0) {
        size_t pid = vnode->ino / (PROCFS_PROC_ENTRIES + 1);

        if (offset == 0) {
            dirent->d_ino = 0;
            strcpy(dirent->d_name, ".");
            return 1;
        } else if (offset == 1) {
            dirent->d_ino = 1;
            strcpy(dirent->d_name, "..");
            return 1;
        }

        offset -= 2;

        if ((size_t) offset < PROCFS_PROC_ENTRIES) {
            strcpy(dirent->d_name, proc_entries[offset].name);
            return 1;
        }
    }

    return 0;
}

static int procfs_finddir(struct vnode *parent, const char *name, struct dirent *dirent)
{
    //printk("procfs_finddir(parent=%p, name=%s, dirent=%p)\n", parent, name, dirent);

    if (parent->ino == (ino_t) procfs_root) {
        for (size_t i = 0; i < PROCFS_ENTRIES; ++i) {
            if (!strcmp(name, entries[i].name)) {
                dirent->d_ino  = -(i + 1);
                return 0;
            }
        }

        if (!strcmp(name, "self")) {
            dirent->d_ino = curproc->pid * (PROCFS_PROC_ENTRIES + 1);
            return 0;
        }

        queue_for (node, procs) {
            struct proc *proc = node->value;
            char buf[10];
            snprintf(buf, 10, "%d", proc->pid);

            if (!strcmp(buf, name)) {
                dirent->d_ino = proc->pid * (PROCFS_PROC_ENTRIES + 1);
                return 0;
            }
        }
    } else if (parent->ino > 0) {
        size_t pid = parent->ino / (PROCFS_PROC_ENTRIES + 1);

        for (size_t i = 0; i < PROCFS_PROC_ENTRIES; ++i) {
            if (!strcmp(proc_entries[i].name, name)) {
                dirent->d_ino  = pid * (PROCFS_PROC_ENTRIES + 1) + i + 1;
                return 0;
            }
        }
    }

    return -ENOENT;
}

static int procfs_vget(struct vnode *super, ino_t ino, struct vnode **vnode)
{
    //printk("procfs_vget(super=%p, ino=%d, vnode=%p)\n", super, ino, vnode);

    int err = 0;

    if (ino == (vino_t) procfs_root) {
        if (vnode) *vnode = procfs_root;
        return 0;
    } else if ((ssize_t) ino < 0) {
        intptr_t id = -ino - 1;

        if ((size_t) id < PROCFS_ENTRIES) {
            struct vnode *node = NULL;
            if (!(node = vcache_find(&procfs_vcache, id))) { /* Not in open vnode table? */
                node = kmalloc(sizeof(struct vnode), &M_VNODE, M_ZERO);
                if (!node) goto e_nomem;

                node->ino  = ino;
                node->mode = S_IFREG | 0555;
                node->fs   = &procfs;
                node->p    = (void *) id;

                struct timespec ts;
                gettime(&ts);

                node->ctime = ts;
                node->atime = ts;
                node->mtime = ts;

                vcache_insert(&procfs_vcache, node);
            }

            if (vnode) *vnode = node;
            return 0;
        } else {
            return -ENONET;
        }
    } else {
        size_t pid = ino / (PROCFS_PROC_ENTRIES + 1);
        size_t id  = ino % (PROCFS_PROC_ENTRIES + 1);
        struct vnode *node = NULL;

        if (!(node = vcache_find(&procfs_vcache, ino))) { /* Not in open vnode table? */
            node = kmalloc(sizeof(struct vnode), &M_VNODE, M_ZERO);
            if (!node) goto e_nomem;

            node->ino = ino;

            if (id) {
                //node->name = proc_entries[id].name;
                node->mode = S_IFREG | 0555;
            } else {
                node->mode = S_IFDIR | 0555;
            }

            node->fs   = &procfs;
            node->p    = (void *) id;

            struct timespec ts;
            gettime(&ts);

            node->ctime = ts;
            node->atime = ts;
            node->mtime = ts;

            vcache_insert(&procfs_vcache, node);
        }

        if (vnode) *vnode = node;

        return 0;
    }

e_nomem:
    err = -ENOMEM;
error:
    return err;
}

static int procfs_close(struct vnode *vnode)
{
    /* we can't free the root vnode */
    if (vnode == procfs_root) {
        /* should we panic here? */
        return 0;
    }

    if (vnode->ref == 0) {
        if (vcache_find(&procfs_vcache, vnode->ino))
            vcache_remove(&procfs_vcache, vnode);

        kfree(vnode);
    }

    return 0;
}

static int procfs_init()
{
    procfs_root = kmalloc(sizeof(struct vnode), &M_VNODE, M_ZERO);
    if (!procfs_root) return -ENOMEM;

    procfs_root->mode = S_IFDIR | 0555;
    procfs_root->ino  = (vino_t) procfs_root;
    procfs_root->fs   = &procfs;
    procfs_root->ref  = 1;

    struct timespec ts;
    gettime(&ts);

    procfs_root->ctime = ts;
    procfs_root->atime = ts;
    procfs_root->mtime = ts;

    vcache_init(&procfs_vcache);

    vfs_install(&procfs);
    return 0;
}

static int procfs_mount(const char *dir, int flags, void *data)
{
    vfs_bind(dir, procfs_root);
    return 0;
}

struct fs procfs = {
    .name  = "procfs",
    .nodev = 1,
    .init  = procfs_init,
    .mount = procfs_mount,

    .vops = {
        .read    = procfs_read,
        .readdir = procfs_readdir,
        .finddir = procfs_finddir,
        .vget    = procfs_vget,
        .close   = procfs_close,
    },

    .fops = {
        .open    = posix_file_open,
        .read    = posix_file_read,
        .readdir = posix_file_readdir,
        .close   = posix_file_close,

        .eof     = __vfs_eof_always, /* XXX */
    },
};

MODULE_INIT(procfs, procfs_init, NULL)
