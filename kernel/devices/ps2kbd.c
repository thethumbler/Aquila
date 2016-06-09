#include <core/system.h>
#include <cpu/cpu.h>
#include <dev/dev.h>
#include <ds/ring.h>
#include <fs/devfs.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <core/arch.h>
#include <bits/errno.h>
#include <ds/queue.h>
#include <bits/fcntl.h>

#define BUF_SIZE	128

static ring_t *kbd_ring;	/* Keboard Ring Buffer */
static proc_t *proc = NULL;	/* Current process using Keboard */
static queue_t *kbd_read_queue = &(queue_t){0};	/* Keyboard read queue */

void ps2kbd_handler(int scancode)
{
	ring_write(kbd_ring, sizeof(scancode), (char*)&scancode);
	
	if(kbd_read_queue->count)
		wakeup_queue(kbd_read_queue);
}

void ps2kbd_register()
{
	extern void i8042_register_handler(int channel, void (*fun)(int));
	i8042_register_handler(1, ps2kbd_handler);
}

size_t ps2kbd_read(inode_t *inode __unused, size_t offset __unused, size_t size, void *buf)
{
	return ring_read(kbd_ring, size, buf);
}

int ps2kbd_probe()
{
	ps2kbd_register();
	kbd_ring = new_ring(BUF_SIZE);

	inode_t *kbd = vfs.create(dev_root, "kbd");

	kbd->dev = &ps2kbddev;

	return 0;
}

/* File Operations */

static int ps2kbd_file_open(fd_t *fd)
{
	if(proc) /* Only one process can open kbd */
		return -EACCES;

	proc = cur_proc;
	return vfs_generic_file_open(fd);
}

dev_t ps2kbddev = (dev_t)
{
	.name  = "kbddev",
	.type  = CHRDEV,
	.probe = ps2kbd_probe,
	.read  = ps2kbd_read,

	.f_ops = 
	{
		.open = ps2kbd_file_open,
		.read = vfs_generic_file_read,
	},
};