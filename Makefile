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
#   make run          boot the kernel and initrd in QEMU, console on stdio
#   make run-iso      boot the ISO in QEMU (framebuffer console)
#   make test         kernel test suite (tests/run.sh, uses the host gcc)
#   make clean        remove build output except the toolchain and downloads
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
QEMU   := qemu-system-i386

MAKEFLAGS += --no-print-directory

export

.PHONY: all toolchain kernel userland contrib ports initrd iso run run-iso test clean distclean check-toolchain

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
AQBOX_SBIN := getty login mount readmbr reboot

# contrib/ is built unless it comes from elsewhere
initrd: userland $(if $(filter $(BUILDDIR)/contrib/root,$(CONTRIB_ROOT)),contrib)
	@$(ECHO) "  ROOTFS  " $(patsubst $(BUILDDIR)/%,build/%,$(ROOTFS))
	@$(RM) -r $(ROOTFS)
	@$(MKDIR) $(ROOTFS)
	@cd $(ROOTFS) && $(MKDIR) bin sbin dev mnt proc tmp root home/live
	@$(CP) -r $(SRCDIR)/rootfs/. $(ROOTFS)/
	@if [ -n "$(ROOTFS_OVERLAY)" ]; then $(CP) -r $(ROOTFS_OVERLAY)/. $(ROOTFS)/; fi
	@$(CP) $(BUILDDIR)/userland/bin/init $(ROOTFS)/init
	@$(CP) $(BUILDDIR)/userland/bin/aqbox $(ROOTFS)/bin/
	@chmod 600 $(ROOTFS)/etc/shadow
	@for a in $(AQBOX_BIN); do $(LN) -s /bin/aqbox $(ROOTFS)/bin/$$a; done
	@for a in $(AQBOX_SBIN); do $(LN) -s /bin/aqbox $(ROOTFS)/sbin/$$a; done
	@test -d $(CONTRIB_ROOT) || { echo "error: $(CONTRIB_ROOT) not found" >&2; exit 1; }
	@# replace, not write through, a symbolic link of the same name
	@$(CP) -r --remove-destination $(CONTRIB_ROOT)/. $(ROOTFS)/
	@if [ -d $(PORTS_ROOT) ]; then \
		$(CP) -r --remove-destination $(PORTS_ROOT)/. $(ROOTFS)/; fi
	@find $(ROOTFS) -type f -exec sh -c \
		'for f; do head -c 4 "$$f" | grep -q ELF && $(STRIP) "$$f"; done' _ {} +
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

run: kernel initrd
	$(QEMU) -kernel $(KERNEL) -initrd "$(INITRD) --serial" -append earlycon=ttyS0 \
		-m 256 -serial mon:stdio -display none

run-iso: iso
	$(QEMU) -cdrom $(ISO) -m 256 -serial stdio

test:
	@$(SRCDIR)/tests/run.sh

clean:
	@$(RM) -r $(BUILDDIR)/kernel $(BUILDDIR)/userland $(ROOTFS) $(BUILDDIR)/iso \
		$(INITRD) $(ISO) $(BUILDDIR)/contrib $(BUILDDIR)/ports $(BUILDDIR)/staging

distclean:
	@$(RM) -r $(BUILDDIR)
