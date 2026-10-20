#
# AquilaOS top level Makefile
#
# Everything is built into $(BUILDDIR) (default: build/).
#
#   make toolchain    i686-aquila cross toolchain and C library
#                     (build/toolchain, build/sysroot), needed by the rest
#   make              kernel, user space programs, base third-party
#                     software (contrib/) and initrd image
#   make kernel       build/kernel/kernel-$(VERSION).$(ARCH)
#   make userland     build/userland/bin/
#   make contrib      third-party software of the base system in contrib/
#                     (build/contrib/root): dash, sbase
#   make ports        optional third-party software in ports/
#                     (build/ports/root), included in the initrd once built
#
#   CONTRIB_ROOT=<dir> and PORTS_ROOT=<dir> take those from elsewhere.
#   make initrd       build/rootfs/ and build/initrd.img, ROOTFS_OVERLAY=<dir>
#                     is copied over rootfs/ (e.g. etc/passwd and etc/shadow)
#   make iso          build/aquila.iso, boots with Limine (needs xorriso)
#   make hdd          build/aquila.img, a hard disk with AquilaOS installed:
#                     Limine on a FAT32 /boot, the root filesystem on ext2
#                     (HDD_SIZE=<MiB>, default 512; needs sfdisk, mkfs.fat,
#                     mtools, mke2fs and fakeroot). Replaces an existing one.
#   make run          boot the kernel and initrd in QEMU, console on stdio
#   make run-iso      boot the ISO in QEMU (framebuffer console)
#   make hdd-kernel   put the kernel (and Limine's configuration) on
#                     build/aquila.img, its root filesystem is kept as it is
#   make hdd-update   the kernel and the system just built (build/rootfs)
#                     on build/aquila.img: what the build has replaces the
#                     disk's, the rest is kept (/root, /home, and the
#                     accounts in /etc if the disk has them)
#   make run-hdd      boot the hard disk in QEMU with the system just built
#                     (make hdd-update), made by make hdd if there is none
#
#   QEMU uses KVM when /dev/kvm can be used, KVM=0 turns it off (also for
#   make test).
#   make test         kernel test suite (tests/run.sh, uses the host gcc)
#   make clean        remove build output except the toolchain, downloads
#                     and the hard disk (it has what was saved on it)
#   make distclean    remove build/
#
# CONFIG selects the kernel configuration in configs/ (default: i386-pc).
#

SRCDIR   := $(CURDIR)
BUILDDIR ?= $(SRCDIR)/build
CONFIG   ?= i386-pc

include $(SRCDIR)/configs/$(CONFIG).mk

VERSION := 0.0.1

TOOLCHAIN ?= $(BUILDDIR)/toolchain
SYSROOT   := $(BUILDDIR)/sysroot
ROOTFS    := $(BUILDDIR)/rootfs
INITRD    := $(BUILDDIR)/initrd.img
ISO       := $(BUILDDIR)/aquila.iso
HDD       := $(BUILDDIR)/aquila.img
HDD_SIZE  ?= 512
KERNEL    := $(BUILDDIR)/kernel/kernel-$(VERSION).$(ARCH)
CONTRIB_ROOT ?= $(BUILDDIR)/contrib/root
PORTS_ROOT   ?= $(BUILDDIR)/ports/root

PATH := $(TOOLCHAIN)/bin:$(PATH)

CP    := cp
ECHO  := echo
MKDIR := mkdir -p
LN    := ln
STRIP := i686-aquila-strip

LIMINE := $(TOOLCHAIN)/share/limine

# KVM when /dev/kvm can be used, KVM=0 emulates the CPU instead
KVM  ?= $(if $(shell test -r /dev/kvm -a -w /dev/kvm && echo y),1,0)
QEMU := qemu-system-i386 $(if $(filter 1,$(KVM)),-accel kvm)

MAKEFLAGS += --no-print-directory

export

.PHONY: all toolchain kernel userland contrib ports initrd iso hdd hdd-kernel hdd-update run run-iso run-hdd test clean distclean check-toolchain

all: kernel userland contrib initrd

toolchain:
	@$(SRCDIR)/toolchain/build.sh

check-toolchain:
	@command -v i686-aquila-gcc >/dev/null || { \
		echo "error: i686-aquila-gcc not found, run 'make toolchain' first" >&2; exit 1; }

# the kernel can also be built with the host compiler (see tests/host/)
kernel:
	@command -v $(CC) >/dev/null || { \
		echo "error: $(CC) not found, run 'make toolchain' first" >&2; exit 1; }
	@$(MAKE) -f $(SRCDIR)/kernel/Makefile

userland: check-toolchain
	@$(MAKE) -f $(SRCDIR)/userland/Makefile

#
# Third-party software: contrib/ (base system) and ports/ (optional)
#

RECIPES := $(SRCDIR)/mk/recipes.sh
CONTRIB := $(patsubst $(SRCDIR)/contrib/%/recipe.sh,%,$(wildcard $(SRCDIR)/contrib/*/recipe.sh))

# rebuilt when its recipe or patches, the build script or libc change
contrib: $(CONTRIB:%=$(BUILDDIR)/contrib/%.built)

.SECONDEXPANSION:
$(BUILDDIR)/contrib/%.built: $$(wildcard $(SRCDIR)/contrib/$$*/*) $(RECIPES) \
		$$(wildcard $(SYSROOT)/usr/lib/libc.a) | check-toolchain
	@BUILDDIR=$(BUILDDIR) TOOLCHAIN=$(TOOLCHAIN) $(RECIPES) contrib $*
	@touch $@

ports: check-toolchain
	@BUILDDIR=$(BUILDDIR) TOOLCHAIN=$(TOOLCHAIN) $(RECIPES) ports

#
# Root filesystem and initrd
#

# what the base system has besides contrib/ (sbase, dash)
AQBOX_BIN  := clear ps stat truncate vmstat
AQBOX_SBIN := getty login mount poweroff readmbr reboot

# contrib/ is built unless it comes from elsewhere
initrd: userland $(if $(filter $(BUILDDIR)/contrib/root,$(CONTRIB_ROOT)),contrib)
	@$(ECHO) "  ROOTFS  " $(patsubst $(BUILDDIR)/%,build/%,$(ROOTFS))
	@$(RM) -r $(ROOTFS)
	@$(MKDIR) $(ROOTFS)
	@cd $(ROOTFS) && $(MKDIR) bin sbin dev mnt proc run tmp root home/live
	@$(CP) -r $(SRCDIR)/rootfs/. $(ROOTFS)/
	@if [ -n "$(ROOTFS_OVERLAY)" ]; then $(CP) -r $(ROOTFS_OVERLAY)/. $(ROOTFS)/; fi
	@# runit is init (contrib/runit)
	@$(LN) -s /sbin/runit-init $(ROOTFS)/init
	@$(CP) $(BUILDDIR)/userland/bin/aqbox $(ROOTFS)/bin/
	@chmod 600 $(ROOTFS)/etc/shadow
	@for a in $(AQBOX_BIN); do $(LN) -s /bin/aqbox $(ROOTFS)/bin/$$a; done
	@for a in $(AQBOX_SBIN); do $(LN) -s /bin/aqbox $(ROOTFS)/sbin/$$a; done
	@test -d $(CONTRIB_ROOT) || { echo "error: $(CONTRIB_ROOT) not found" >&2; exit 1; }
	@# replace, not write through, a symbolic link of the same name
	@$(CP) -r --remove-destination $(CONTRIB_ROOT)/. $(ROOTFS)/
	@if [ -d $(PORTS_ROOT) ]; then \
		$(CP) -r --remove-destination $(PORTS_ROOT)/. $(ROOTFS)/; fi
	@# programs, not object files (crt*.o), whose symbols linking needs
	@find $(ROOTFS) -type f ! -name '*.o' -exec sh -c \
		'for f; do if head -c 4 "$$f" | grep -q ELF; then $(STRIP) "$$f"; fi; done' _ {} +
	@$(ECHO) "  CPIO    " $(patsubst $(BUILDDIR)/%,build/%,$(INITRD))
	@# the kernel's initramfs reads old binary cpio archives
	@cd $(ROOTFS) && find . | cpio -o -H bin -R 0:0 --quiet > $(INITRD)

#
# Bootable image
#

iso: kernel initrd
	@test -f $(LIMINE)/limine-bios-cd.bin || { \
		echo "error: Limine not found, run 'make toolchain'" >&2; exit 1; }
	@$(ECHO) "  ISO     " $(patsubst $(BUILDDIR)/%,build/%,$(ISO))
	@$(RM) -r $(BUILDDIR)/iso
	@$(MKDIR) $(BUILDDIR)/iso/boot/limine
	@$(CP) $(KERNEL) $(BUILDDIR)/iso/boot/kernel
	@$(CP) $(INITRD) $(BUILDDIR)/iso/boot/initrd.img
	@$(CP) $(SRCDIR)/boot/limine.conf $(LIMINE)/limine-bios.sys \
		$(LIMINE)/limine-bios-cd.bin $(BUILDDIR)/iso/boot/limine/
	@xorriso -as mkisofs -R -r -J -b boot/limine/limine-bios-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table \
		$(BUILDDIR)/iso -o $(ISO) 2>/dev/null

hdd: kernel initrd
	@test -x $(TOOLCHAIN)/bin/limine || { \
		echo "error: Limine not found, run 'make toolchain'" >&2; exit 1; }
	@$(ECHO) "  HDD     " $(patsubst $(BUILDDIR)/%,build/%,$(HDD))
	@$(SRCDIR)/mk/hdd.sh $(HDD) $(HDD_SIZE) $(ROOTFS) $(KERNEL) \
		$(SRCDIR)/boot/limine-hdd.conf $(LIMINE) $(TOOLCHAIN)/bin/limine

hdd-kernel: kernel
	@test -f $(HDD) || { \
		echo "error: $(patsubst $(BUILDDIR)/%,build/%,$(HDD)) not found, run 'make hdd'" >&2; exit 1; }
	@$(ECHO) "  KERNEL  " $(patsubst $(BUILDDIR)/%,build/%,$(HDD))
	@$(SRCDIR)/mk/hdd-kernel.sh $(HDD) $(KERNEL) $(SRCDIR)/boot/limine-hdd.conf

hdd-update: hdd-kernel initrd
	@$(ECHO) "  UPDATE  " $(patsubst $(BUILDDIR)/%,build/%,$(HDD))
	@$(SRCDIR)/mk/hdd-update.sh $(HDD) $(ROOTFS)

run: kernel initrd
	$(QEMU) -kernel $(KERNEL) -initrd "$(INITRD)" -append earlycon=ttyS0 \
		-m 256 -serial mon:stdio -display none

run-iso: iso
	$(QEMU) -cdrom $(ISO) -m 256 -serial stdio

run-hdd:
	@if [ -f $(HDD) ]; then $(MAKE) hdd-update; else $(MAKE) hdd; fi
	$(QEMU) -hda $(HDD) -m 256 -serial stdio

test:
	@$(SRCDIR)/tests/run.sh

clean:
	@$(RM) -r $(BUILDDIR)/kernel $(BUILDDIR)/userland $(ROOTFS) $(BUILDDIR)/iso \
		$(INITRD) $(ISO) $(BUILDDIR)/contrib $(BUILDDIR)/ports $(BUILDDIR)/staging

distclean:
	@$(RM) -r $(BUILDDIR)
