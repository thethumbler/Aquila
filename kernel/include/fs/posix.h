#ifndef __VFS_POSIX
#define __VFS_POSIX

#include <fs/vfs.h>

int     posix_file_open(struct file *file);
int     posix_file_close(struct file *file);
ssize_t posix_file_read(struct file *file, void *buf, size_t size);
ssize_t posix_file_write(struct file *file, void *buf, size_t size);
ssize_t posix_file_readdir(struct file *file, struct dirent *dirnet);
int     posix_file_ioctl(struct file *file, int request, void *argp);
off_t   posix_file_lseek(struct file *file, off_t offset, int whence);

/* helpers */
int     posix_file_can_read(struct file *file, size_t size);
int     posix_file_can_write(struct file *file, size_t size);
int     posix_file_eof(struct file *file);

#endif /* ! __VFS_POSIX */
