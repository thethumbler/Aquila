#include <core/system.h>
#include <core/string.h>
#include <core/arch.h>
#include <sys/proc.h>
#include <sys/sched.h>

static void sys_exit(int code __attribute__((unused)))
{
	printk("Recieved sys_exit\n");
	for(;;);
	//kill_process(cur_proc);
	/* We should signal the parent here */
	//kernel_idle();
}

static void sys_fork()
{
	proc_t *fork = fork_proc(cur_proc);
	if(fork)
		enqueue_process(fork);
}

static void sys_open(const char *fn, int flags)
{
	inode_t *inode = vfs.find(vfs_root, fn);

	if(!inode)
		arch_syscall_return(cur_proc, -1);

	int fd = vfs.open(inode, flags);

	arch_syscall_return(cur_proc, fd);
}

static void sys_read(int fd, void *buf, size_t count)
{
	inode_t *inode = cur_proc->fds[fd].inode;
	size_t  offset = cur_proc->fds[fd].offset;

	int ret = vfs.read(inode, offset, count, buf);
	arch_syscall_return(cur_proc, ret);
}

static void sys_write(int fd, void *buf, size_t count)
{
	inode_t *inode = cur_proc->fds[fd].inode;
	size_t  offset = cur_proc->fds[fd].offset;

	int ret = vfs.write(inode, offset, count, buf);
	arch_syscall_return(cur_proc, ret);
}

static void sys_ioctl(int fd, unsigned long request, void *argp)
{
	inode_t *inode = cur_proc->fds[fd].inode;

	int ret = vfs.ioctl(inode, request, argp);
	arch_syscall_return(cur_proc, ret);
}

static void sys_execve(const char *name, char * const argp[], char * const envp[])
{
	char *fn = strdup(name);
	proc_t *p = execve_proc(cur_proc, fn, argp, envp);
	kfree(fn);

	if(!p)
		arch_syscall_return(cur_proc, -1);
	else
		spawn_proc(p);
}

void (*syscall_table[])() = 
{
	NULL,
	sys_exit,
	sys_fork,
	sys_open,
	sys_read,
	sys_write,
	sys_ioctl,
	sys_execve,
};