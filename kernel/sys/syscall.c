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
	printk("sys_fork\n");
	proc_t *fork = kmalloc(sizeof(proc_t));
	memcpy(fork, cur_proc, sizeof(proc_t));

	fork->name = strdup(cur_proc->name);
	fork->pid = get_pid();
	
	fork->fds = kmalloc(FDS_COUNT * sizeof(file_list_t));
	memcpy(fork->fds, cur_proc->fds, FDS_COUNT * sizeof(file_list_t));

	arch_sys_fork(fork);

	arch_user_return(fork, 0);
	arch_user_return(cur_proc, fork->pid);

	enqueue_process(fork);
}

static void sys_dum()
{
	printk("sys_dum\n");
	x86_proc_t *arch = cur_proc->arch;
	printk("pid %d\n", arch->stat.ebx);
	if(cur_proc->pid == 2) for(;;);
}

static void sys_open(const char *fn, int flags)
{
	inode_t *inode = vfs.find(vfs_root, fn);
	if(!inode)
		arch_user_return(cur_proc, -1);

	int fd = vfs.open(inode, flags);

	arch_user_return(cur_proc, fd);
}

static void sys_read(int fd, void *buf, size_t count)
{
	inode_t *inode = cur_proc->fds[fd].inode;
	size_t  offset = cur_proc->fds[fd].offset;

	int ret = vfs.read(inode, offset, count, buf);
	arch_user_return(cur_proc, ret);
}

static void sys_write(int fd, void *buf, size_t count)
{
	inode_t *inode = cur_proc->fds[fd].inode;
	size_t  offset = cur_proc->fds[fd].offset;

	int ret = vfs.write(inode, offset, count, buf);
	arch_user_return(cur_proc, ret);
}

void (*syscall_table[])() = 
{
	NULL,
	sys_exit,
	sys_fork,
	sys_open,
	sys_read,
	sys_write,
	sys_dum,
};