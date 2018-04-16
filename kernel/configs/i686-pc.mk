#
# Compilation flags
#

CC := $(PDIR)/../build-tools/sys/bin/i686-elf-gcc
CFLAGS := -std=gnu99 -I. -I$(PDIR)/arch/$$(ARCH_DIR)/chipset/$$(CHIPSET_DIR)/include -I$(PDIR)/arch/$$(ARCH_DIR)/include -I $(PDIR) -I $(PDIR)/include/ -nostdlib -ffreestanding -m32 \
		  -O3 -Wall -Wextra -Werror -funsigned-bitfields -fuse-ld=bfd \
		  -Wno-unused -march=i586

AS := $(CC)
ASFLAGS := $(CFLAGS)

LD := $(PDIR)/../build-tools/sys/bin/i686-elf-ld.bfd
LDFLAGS := -nostdlib -melf_i386

#
# Configurations
#

ARCH=X86
ARCH_DIR=x86
ARCH_X86=y
ARCH_BITS=32

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

#
# Framebuffer
#

FBDEV_VESA=y

# Filesystems
FS_TMPFS=y
FS_DEVFS=y
FS_DEVPTS=y
FS_EXT2=y
FS_PROCFS=y
