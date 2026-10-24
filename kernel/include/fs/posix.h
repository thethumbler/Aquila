#ifndef __FS_POSIX_H
#define __FS_POSIX_H

#include <fs/vfs.h>

/* The usual file operations (fs/posix/), which most filesystems and
 * devices use: they call the vnode's operations. */

int     posix_file_open(struct file *file);
int     posix_file_close(struct file *file);
ssize_t posix_file_read(struct file *file, struct iobuf *io);
ssize_t posix_file_write(struct file *file, struct iobuf *io);
ssize_t posix_file_readdir(struct file *file, struct dirent *dirent);
int     posix_file_ioctl(struct file *file, int request, void *argp);
off_t   posix_file_lseek(struct file *file, off_t offset, int whence);
int     posix_file_trunc(struct file *file, off_t len);

/* For select() and poll(), and end of file: what most files say */
int     posix_file_can_read(struct file *file, size_t size);
int     posix_file_can_write(struct file *file, size_t size);
int     posix_file_eof(struct file *file);

#endif /* ! __FS_POSIX_H */
