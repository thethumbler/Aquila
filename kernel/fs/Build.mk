# Mandatory targets
dirs-y += virtfs/
dirs-y += initramfs/
dirs-y += posix/
obj-y += vfs.o
obj-y += pipe.o
obj-y += itbl.o
obj-y += ubc.o

# Optional targets
dirs-$(FS_TMPFS)  += tmpfs/
dirs-$(FS_DEVFS)  += devfs/
dirs-$(FS_DEVPTS) += devpts/
dirs-$(FS_EXT2)   += ext2/
dirs-$(FS_PROCFS) += procfs/
