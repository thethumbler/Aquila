#
# Compilation flags
#

CC := $(PDIR)/../build-tools/sysroot/bin/x86_64-elf-gcc
CFLAGS := -std=gnu99 -I. -I$(PDIR)/arch/$$(ARCH_DIR)/chipset/$$(CHIPSET_DIR)/include -I$(PDIR)/arch/$$(ARCH_DIR)/include -I $(PDIR) -I $(PDIR)/include/ -nostdlib -ffreestanding \
		  -O3 -Wall -Wextra -Werror -funsigned-bitfields -fuse-ld=bfd \
		  -Wno-unused \
		  -mcmodel=large -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -z max-page-size=0x1000

AS := $(CC)
ASFLAGS := $(CFLAGS)

LD := $(PDIR)/../build-tools/sysroot/bin/x86_64-elf-ld.bfd
LDFLAGS := -nostdlib
#-melf64_x86_64

#
# Configurations
#

ARCH=X86_64
ARCH_DIR=x86_64
ARCH_X86_64=y
ARCH_BITS=64

#
# Boot Definitions
#

X86_MULTIBOOT=y

#
# Chipset - generic
#

CHIPSET_X86_GENERIC=y
CHIPSET_DIR=generic
# Support 8259 PIC
CHIPSET_X86_MISC_PIC=y
# Support 8253/8254 PIT
CHIPSET_X86_MISC_PIT=y
# Support i8042 PS/2 Controller
CHIPSET_X86_MISC_I8042=y

#
# Devices
#

# Generic framebuffer (fbdev) support
DEV_FRAMEBUFFER=y
# Generic console (condev) support
DEV_CONSOLE=y
DEV_KEYBOARD=y
DEV_MOUSE=y
DEV_KEYBOARD_PS2=y
DEV_UART=y
DEV_PTY=y
DEV_MEMDEV=y
DEV_ATA=y

#
# Framebuffer
#

FBDEV_VESA=y

# Filesystems
FS_TMPFS=y
FS_DEVFS=y
FS_DEVPTS=y
#FS_EXT2=y
FS_PROCFS=y

# Initramfs
INITRAMFS_CPIO=y
