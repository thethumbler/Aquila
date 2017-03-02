/*
 *			VFS => Generic file read function
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
 * generic_file_read
 *
 * Reads up to `size' bytes from a file to `buf'.
 * Conforming to `IEEE Std 1003.1, 2013 Edition'
 * 
 * @file 	File Descriptor for the function to operate on.
 * @buf  	Buffer to write to.
 * @size 	Number of bytes to read.
 * @returns read bytes on success, or error-code on failure.
 */

ssize_t generic_file_read(struct file *file, void *buf, size_t size)
{
	if (file->flags & O_WRONLY)	/* File is not opened for reading */
		return -EBADFD;
	
	if (file->flags & O_NONBLOCK) {	/* Non-blocking I/O */
		
		if (file->node->fs->f_ops.can_read(file, size)) {
			
			/* Read up to `size' from file into `buf' */
			ssize_t retval = file->node->fs->read(file->node, file->offset, size, buf);

			/* Update file offset */
			file->offset += retval;
			
			/* Wake up all sleeping writers if a `write_queue' is attached */
			if (file->node->write_queue)
				wakeup_queue(file->node->write_queue);

			/* Return read bytes count */
			return retval;
		}
		else {

			/* Can not satisfy read operation, would block */
			return -EAGAIN;
		}
	} else {	/* Blocking I/O */
		
		size_t retval = size;
		
		while (size) {

			size -= file->node->fs->read(file->node, file->offset, size, buf);

			if (file->node->type == FS_PIPE && size != retval)
				break;

			/* No bytes left to read, or reached END-OF-FILE */
			if (!size || file->node->fs->f_ops.eof(file))	/* Done reading */
				break;

			/* Sleep on the file readers queue */
			sleep_on(file->node->read_queue);
		}
		
		/* Store read bytes count */
		retval -= size;

		/* Update file offset */
		file->offset += retval;

		/* Wake up all sleeping writers if a `write_queue' is attached */
		if (file->node->write_queue)
			wakeup_queue(file->node->write_queue);

		return retval;
	}
}
