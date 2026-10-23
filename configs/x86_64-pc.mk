CC := x86_64-aquila-gcc

# the kernel model: linked in the top 2 GiB; no red zone (interrupts use
# the kernel stack), no SSE (its registers aren't saved on entry)
CFLAGS += \
		-std=gnu11 -nostdlib -ffreestanding -m64 -mcmodel=kernel \
		-mno-red-zone -mgeneral-regs-only -fno-omit-frame-pointer \
		-O3 -Wall -Wextra -Werror \
		-Wno-unused -Wno-unused-parameter \
		-funsigned-bitfields -fuse-ld=bfd

AS := $(CC)
ASFLAGS := $(CFLAGS)
LD := x86_64-aquila-ld
# the kernel is mapped as a single RWX segment
LDFLAGS := -nostdlib -melf_x86_64 -z max-page-size=0x1000 --no-warn-rwx-segments

#
# Configurations
#

ARCH=x86_64
ARCH_DIR=x86_64
# the system's compilers
TARGET=x86_64-aquila
ARCH_X86_64=y
ARCH_BITS=64

#
# Boot Definitions
#

X86_MULTIBOOT=y

#
# Platform - PC
#

PLATFORM_X86_PC=y
PLATFORM_DIR=pc
# Support 8259 PIC
PLATFORM_X86_MISC_PIC=y
# Support 8253/8254 PIT
PLATFORM_X86_MISC_PIT=y
# Support the APIC (the 8259s without it, or with apic=off)
PLATFORM_X86_MISC_APIC=y
# Support HPET
PLATFORM_X86_MISC_HPET=y
# Support ACPI
PLATFORM_X86_MISC_ACPI=y
# Support CMOS
PLATFORM_X86_MISC_CMOS=y

#
# Devices
#

# Generic framebuffer (fbdev) support
DEV_FRAMEBUFFER=y
# Virtual terminals (vt) on the framebuffer or VGA text mode
DEV_CONSOLE=y
# VGA text mode, when there's no framebuffer
DEV_CONSOLE_VGA=y
# the PC's keyboard controller (i8042): PS/2 keyboard and mouse
DEV_I8042=y
DEV_KEYBOARD=y
DEV_MOUSE=y
DEV_KEYBOARD_PS2=y
DEV_UART=y
# the PC's serial port (16550)
DEV_UART_8250=y
DEV_PTY=y
DEV_MEMDEV=y
# the PCI bus (configuration mechanism #1, the PC's ports)
DEV_PCI=y
DEV_ATA=y
# Intel 8254x Ethernet (e1000)
DEV_E1000=y

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
FS_MINIX=y

# Initramfs
INITRAMFS_CPIO=y

# Network
NET_UNIX=y
# IPv4 over Ethernet
NET_INET=y
