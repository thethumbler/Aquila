#include <core/system.h>
#include <core/string.h>
#include <bits/errno.h>

#include <fs/vfs.h>
#include <fs/devfs.h>
#include <fs/posix.h>
#include <fs/procfs.h>
#include <fs/itbl.h>

#include <sys/proc.h>
#include <sys/sched.h>

struct procfs_entry {
    int id;
    char *name;
    ssize_t (*read)();
};

/* procfs root directory (usually mounted on /proc) */
struct inode *procfs_root = NULL;
struct vnode vprocfs_root;
struct itbl  procfs_itbl = {0};

/* proc/meminfo */
static ssize_t procfs_meminfo(struct inode *node __unused, off_t off, size_t size, char *buf)
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

/* proc/version */
static ssize_t procfs_version(struct inode *node __unused, off_t off, size_t size, char *buf)
{
    char version_buf[512];

    int sz = snprintf(version_buf, 512,
            "%s version %s (gcc version %s) %s\n",
            UTSNAME_SYSNAME, UTSNAME_RELEASE, __VERSION__, __TIMESTAMP__
            );

    if (off < sz) {
        ssize_t ret = MIN(size, (size_t)(sz - off));
        memcpy(buf, version_buf + off, ret);
        return ret;
    }

    return 0;
}

/* proc/uptime */
static ssize_t procfs_uptime(struct inode *node __unused, off_t off, size_t size, char *buf)
{
    char uptime_buf[64];

    extern uint32_t timer_ticks;
    int sz = snprintf(uptime_buf, 64, "%d\n", timer_ticks);

    if (off < sz) {
        ssize_t ret = MIN(size, (size_t)(sz - off));
        memcpy(buf, uptime_buf + off, ret);
        return ret;
    }

    return 0;
}

static struct procfs_entry entries[] = {
    {-1, "meminfo", procfs_meminfo},
    {-2, "version", procfs_version},
    {-3, "uptime",  procfs_uptime},
};

#define PROCFS_ENTRIES  (sizeof(entries)/sizeof(entries[0]))

static ssize_t procfs_read(struct inode *node, off_t offset, size_t size, void *buf)
{
    size_t id = (size_t) node->p;
    if (id < PROCFS_ENTRIES) {
        return entries[id].read(node, offset, size, buf);
    }

    return -ENOSYS;
}

static ssize_t procfs_readdir(struct inode *inode __unused, off_t offset, struct dirent *dirent)
{
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
        dirent->d_ino = entries[offset].id;
        strcpy(dirent->d_name, entries[offset].name);
        return 1;
    }

    /* Processes go here */
    offset -= PROCFS_ENTRIES;

    forlinked (node, procs->head, node->next) {
        if (!offset) {
            proc_t *proc = node->value;
            snprintf(dirent->d_name, 10, "%d", proc->pid);
            return 1;
        }

        --offset;
    }

    return 0;
}

static int procfs_vfind(struct vnode *parent, const char *name, struct vnode *child)
{
    if (parent->id == (vino_t) procfs_root) {
        for (size_t i = 0; i < PROCFS_ENTRIES; ++i) {
            if (!strcmp(name, entries[i].name)) {
                child->id = entries[i].id;
                child->type = FS_RGL;
                child->mask = 0444;
                return 0;
            }
        }
    }

    return -1;
}

static int procfs_vget(struct vnode *vnode, struct inode **inode)
{
    if (vnode->id == (vino_t) procfs_root) {
        if (inode)
            *inode = procfs_root;
        return 0;
    } else if ((ssize_t) vnode->id < 0) {
        int id = -vnode->id - 1;

        if ((size_t) id < PROCFS_ENTRIES) {
            struct inode *node = NULL;
            if (!(node = itbl_find(&procfs_itbl, id))) { /* Not in open inode table? */
                node = kmalloc(sizeof(struct inode));
                memset(node, 0, sizeof(struct inode));

                node->name = entries[id].name;
                node->type = FS_RGL;
                node->fs   = &procfs;
                node->p    = (void *) id;

                itbl_insert(&procfs_itbl, node);
            }

            node->ref++;

            if (inode)
                *inode = node;

            return 0;
        } else {
            return -ENONET;
        }
    }

    return -1;
}

static int procfs_init()
{
    procfs_root = kmalloc(sizeof(struct inode));

    if (!procfs_root)
        return -ENOMEM;

    memset(procfs_root, 0, sizeof(struct inode));

    *procfs_root = (struct inode) {
        .type = FS_DIR,
        .id   = (vino_t) procfs_root,
        .fs   = &procfs,
    };

    vprocfs_root = (struct vnode) {
        .super = procfs_root,
        .id    = (vino_t) procfs_root,
        .type  = FS_DIR,
    };

    itbl_init(&procfs_itbl);

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
    .init  = procfs_init,
    .mount = procfs_mount,

    .iops = {
        .read    = procfs_read,
        .readdir = procfs_readdir,
        .vfind   = procfs_vfind,
        .vget    = procfs_vget,
    },

    .fops = {
        .open    = posix_file_open,
        .read    = posix_file_read,
        .readdir = posix_file_readdir,

        .eof     = __vfs_always,
    },
};

MODULE_INIT(procfs, procfs_init, NULL)
