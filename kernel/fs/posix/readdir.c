/*
 *          VFS => Generic file readdir function
 *
 *
 *  This file is part of Aquila OS and is released under
 *  the terms of GNU GPLv3 - See LICENSE.
 *
 *  Copyright (C) 2016-2017 Mohamed Anwar <mohamed_anwar@opmbx.org>
 *
 */

#include <core/system.h>

#include <fs/vfs.h>

#include <bits/fcntl.h>
#include <bits/errno.h>
#include <bits/dirent.h>

/**
 * posix_file_readdir
 *
 * Conforming to `IEEE Std 1003.1, 2013 Edition'
 * 
 * @file    File Descriptor for the function to operate on.
 * @dirent  Buffer to write to.
 */

ssize_t posix_file_readdir(struct file *file, struct dirent *dirent)
{
    if (file->flags & O_WRONLY) /* File is not opened for reading */
        return -EBADFD;
    
    int retval = vfs_readdir(file->node, file->offset, dirent);

    /* Update file offset */
    file->offset += retval;
        
    /* Return read bytes count */
    return retval;
}
