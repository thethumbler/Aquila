obj-y  += close.o
obj-y  += fops.o
obj-y  += ioctl.o
obj-y  += lookup.o
obj-y  += mknod.o
obj-y  += mount.o
obj-y  += readdir.o
obj-y  += read.o
obj-y  += stat.o
obj-y  += sync.o
obj-y  += trunc.o
obj-y  += unlink.o
obj-y  += vops.o
obj-y  += write.o

obj-y  += vfs.o

# Mandatory targets
dirs-y += pseudofs/
dirs-y += initramfs/
dirs-y += posix/
obj-y  += pipe.o
obj-y  += rofs.o
obj-y  += vcache.o
obj-y  += bcache.o
obj-y  += vm_object.o

# Optional targets
dirs-$(FS_TMPFS)  += tmpfs/
dirs-$(FS_DEVFS)  += devfs/
dirs-$(FS_DEVPTS) += devpts/
dirs-$(FS_EXT2)   += ext2/
dirs-$(FS_PROCFS) += procfs/
dirs-$(FS_MINIX)  += minix/
