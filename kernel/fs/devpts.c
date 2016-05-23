#include <core/system.h>
#include <core/string.h>
#include <fs/vfs.h>
#include <fs/devfs.h>
#include <fs/devpts.h>
#include <ds/ring.h>
#include <sys/proc.h>
#include <sys/sched.h>

inode_t *devpts_root = NULL;

#define PTY_BUF	512

static dev_t ptmdev;
static dev_t ptsdev;

typedef struct pty
{
	int 	id;

	inode_t	*master;
	inode_t	*slave;

	ring_t	*in;	/* Slave Input, Master Output */
	ring_t	*out;	/* Slave Output, Master Input */

	proc_t	*proc;	/* Controlling Process */
	proc_t	*fg;	/* Foreground Process */
} pty_t;

size_t ptsfs_read(inode_t *inode, size_t offset __unused, size_t size, void *buf)
{
	pty_t *pty = (pty_t *) inode->p;
	return ring_read(pty->in, size, buf);
}

size_t ptmfs_read(inode_t *inode, size_t offset __unused, size_t size, void *buf)
{
	pty_t *pty = (pty_t *) inode->p;
	return ring_read(pty->out, size, buf);	
}

size_t ptsfs_write(inode_t *inode, size_t offset __unused, size_t size, void *buf)
{
	pty_t *pty = (pty_t *) inode->p;
	return ring_write(pty->in, size, buf);
}

size_t ptmfs_write(inode_t *inode, size_t offset __unused, size_t size, void *buf)
{
	pty_t *pty = (pty_t *) inode->p;
	return ring_write(pty->out, size, buf);	
}

static int get_pty_id()
{
	static int id = 0;
	return id++;
}

inode_t *new_ptm(pty_t *pty)
{
	inode_t *ptm = kmalloc(sizeof(inode_t));
	memset(ptm, 0, sizeof(inode_t));

	*ptm = (inode_t)
	{
		.fs = &devfs,
		.dev = &ptmdev,
		.type = FS_FILE,
		.size = PTY_BUF,
		.p = pty,
	};

	return ptm;
}

inode_t *new_pts(pty_t *pty)
{
	/* FIXME */
	char *name = strdup("pts ");
	name[3] = '0' + pty->id;

	inode_t *pts = vfs.create(devpts_root, name);

	pts->dev = &ptsdev;
	pts->size = PTY_BUF;
	pts->p = pty;

	return pts;
}

void new_pty(proc_t *proc, inode_t **master, inode_t **slave)
{
	pty_t *pty = kmalloc(sizeof(pty_t));
	memset(pty, 0, sizeof(pty_t));

	pty->id = get_pty_id();
	pty->in = new_ring(PTY_BUF);
	pty->out = new_ring(PTY_BUF);

	*master = pty->master = new_ptm(pty);
	*slave  = pty->slave  = new_pts(pty);

	pty->proc = proc;
}

int ptmxfs_open(inode_t *inode __unused, int flags __unused)
{
	int fdm = get_fd(cur_proc);
	int fds = get_fd(cur_proc);

	printk("Openinig pty at %d & %d for %s\n", fdm, fds, cur_proc->name);
	new_pty(cur_proc, &(cur_proc->fds[fdm].inode), &(cur_proc->fds[fds].inode));

	return fdm;
}

static dev_t ptmxdev = (dev_t)
{
	.name = "ptmxfs",
	.open = ptmxfs_open,
};

void devpts_init()
{
	devpts.create = devfs.create;

	devpts_root = kmalloc(sizeof(inode_t));
	memset(devpts_root, 0, sizeof(inode_t));

	*devpts_root = (inode_t)
	{
		.type = FS_DIR,
		.fs   = &devpts,
	};

	inode_t *ptmx = vfs.create(dev_root, "ptmx");
	ptmx->dev = &ptmxdev;
}

static dev_t ptsdev = (dev_t)
{
	.name = "ptsfs",
	.read = ptsfs_read,
	.write = ptsfs_write,
};

static dev_t ptmdev = (dev_t)
{
	.name = "ptmfs",
	.read = ptmfs_read,
	.write = ptmfs_write,
};

fs_t devpts = (fs_t)
{
	.name = "devpts",
};