obj-y  += mknod.o
obj-y  += stat.o
obj-y  += unlink.o
obj-y  += vfs.o

# Mandatory targets
dirs-y += virtfs/
dirs-y += initramfs/
dirs-y += posix/
obj-y  += pipe.o
obj-y  += icache.o
#obj-y  += ubc.o

# Optional targets
dirs-$(FS_TMPFS)  += tmpfs/
dirs-$(FS_DEVFS)  += devfs/
dirs-$(FS_DEVPTS) += devpts/
dirs-$(FS_EXT2)   += ext2/
dirs-$(FS_PROCFS) += procfs/
dirs-$(FS_MINIX)  += minix/
