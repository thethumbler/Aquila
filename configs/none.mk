CC := $(PDIR)/../build-tools/sysroot/bin/i686-elf-gcc
CFLAGS := -std=gnu99 -I. -I$(PDIR)/arch/$$(ARCH_DIR)/chipset/$$(CHIPSET_DIR)/include -I$(PDIR)/arch/$$(ARCH_DIR)/include -I $(PDIR) -I $(PDIR)/include/ -nostdlib -ffreestanding -m32 \
		  -O3 -Wall -Wextra -Werror -funsigned-bitfields -fuse-ld=bfd \
		  -Wno-unused -march=i386

AS := $(CC)
ASFLAGS := $(CFLAGS)

LD := ld.bfd
LDFLAGS := -nostdlib -melf_i386 -L /opt/aquila/lib/gcc/i686-aquila/7.3.0/

ARCH=none
ARCH_DIR=none
ARCH_NONE=y
ARCH_BITS=32

#DEV_FRAMEBUFFER=y
#DEV_CONSOLE=y
#DEV_KEYBOARD=y
#DEV_MOUSE=y
#DEV_KEYBOARD_PS2=y
#DEV_UART=y
#DEV_PTY=y
#DEV_MEMDEV=y
#DEV_ATA=y
#DEV_FDC=y

FBDEV_VESA=y

# Filesystems
FS_TMPFS=y
FS_DEVFS=y
FS_DEVPTS=y
FS_EXT2=y
FS_PROCFS=y
FS_MINIX=y

# Initramfs
INITRAMFS_CPIO=y
