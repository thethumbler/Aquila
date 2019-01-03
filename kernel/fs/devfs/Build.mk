ifneq ($(FS_TMPFS),y)
$(error "devfs depends on tmpfs")
endif

obj-y += devfs.o
