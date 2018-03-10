#ifndef __VFS_POSIX
#define __VFS_POSIX

#include <fs/vfs.h>

/* kernel/fs/read.c */
ssize_t posix_file_read(struct file *file, void *buf, size_t size);

/* kernel/fs/write.c */
ssize_t posix_file_write(struct file *file, void *buf, size_t size);

/* kernel/fs/readdir.c */
ssize_t posix_file_readdir(struct file *file, struct dirent *dirnet);

/* kernel/fs/posix/ioctl.c */
int posix_file_ioctl(struct file *file, int request, void *argp);

/* kernel/fs/posix/lseek.c */
off_t posix_file_lseek(struct file *file, off_t offset, int whence);

#endif /* ! __VFS_POSIX */
