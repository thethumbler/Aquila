/*
 *			VFS => Generic file write function
 *
 *
 *	This file is part of Aquila OS and is released under
 *	the terms of GNU GPLv3 - See LICENSE.
 *
 *	Copyright (C) 2016 Mohamed Anwar <mohamed_anwar@opmbx.org>
 */

#include <core/system.h>

#include <fs/vfs.h>

#include <bits/fcntl.h>
#include <bits/errno.h>

/**
 * generic_file_write
 *
 * Writes up to `size' bytes from `buf' into a file.
 *
 * @file 	File Descriptor for the function to operate on.
 * @buf  	Buffer to read from.
 * @size 	Number of bytes to write.
 * @returns written bytes on success, or error-code on failure.
 */

ssize_t generic_file_write(struct file *file, void *buf, size_t size)
{
	if(!(file->flags & (O_WRONLY | O_RDWR)))	/* File is not opened for writing */
		return -EBADFD;
	
	if(file->flags & O_NONBLOCK)	/* Non-blocking I/O */
	{
		if(file->node->fs->f_ops.can_write(file, size))
		{
			/* write up to `size' from `buf' into file */
			ssize_t retval = file->node->fs->write(file->node, file->offset, size, buf);

			/* Update file offset */
			file->offset += retval;
			
			/* Wake up all sleeping readers if a `read_queue' is attached */
			if(file->node->read_queue)
				wakeup_queue(file->node->read_queue);

			/* Return written bytes count */
			return retval;
		} else
		{
			/* Can not satisfy write operation, would block */
			return -EAGAIN;
		}
	} else	/* Blocking I/O */
	{
		ssize_t retval = size;
		
		while(size)
		{
			size -= file->node->fs->write(file->node, file->offset, size, buf);

			/* No bytes left to be written, or reached END-OF-FILE */
			if(!size || file->node->fs->f_ops.eof(file))	/* Done writting */
				break;

			/* Sleep on the file writers queue */
			sleep_on(file->node->write_queue);
		}
		
		/* Store written bytes count */
		retval -= size;

		/* Update file offset */
		file->offset += retval;

		/* Wake up all sleeping readers if a `read_queue' is attached */
		if(file->node->read_queue)
			wakeup_queue(file->node->read_queue);

		return retval;
	}
}
