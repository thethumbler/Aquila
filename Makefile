# This is the top leve Makefile of AquilaOS
#
# Targets:
#
#  build-kernel     - build kernel image in kernel/{ARCH}/kernel-{VERSION}.{ARCH}
#  install-kernel   - install kernel image into {DESTDIR}/boot/kernel
#  clean-kernel     - clean kernel source tree
#
#  build-system     - build system components
#  install-system   - install system components into {DESTDIR}/
#  clean-system	    - clean system source tree
#
#  build-initrd     - build ramdisk image in initrd/initrd.img
#  install-initrd   - install ramdisk image into {DESTDIR}/boot/initrd.img
#  clean-initrd     - clean ramdisk source tree
#
#  build-all        - build everything
#  install-all      - install everything
#  clean-all        - clean everything
#

export

ifeq ($(SRCDIR),)
SRCDIR := $(TRAVIS_BUILD_DIR)
endif

ifeq ($(SRCDIR),)
SRCDIR != pwd
endif

ifeq ($(BUILDDIR),)
BUILDDIR != pwd
endif

ifeq ($(CONFIG),)
CONFIG := i386-pc
endif

include $(SRCDIR)/configs/$(CONFIG).mk

ARCH=i386
VERSION=0.0.1
CP=cp
BASH=bash
MKDIR=mkdir -p
ECHO=echo
STRIP=/opt/aquila/bin/i686-aquila-strip
LN=ln

MAKEFLAGS += --no-print-directory

GRUB_MKRESCUE = $(shell command -v grub2-mkrescue 2> /dev/null)
ifeq ($(GRUB_MKRESCUE),)
GRUB_MKRESCUE = grub-mkrescue
endif

DESTDIR = $(SRCDIR)/sysroot

.PHONY: build-all install-all clean-all
build-all: \
	build-kernel \
	build-system \
	build-initrd

install-all: \
	install-kernel \
	install-system \
	install-initrd

clean-all: \
	clean-kernel \
	clean-system \
	clean-initrd

#
# kernel targets
#

.PHONY: build-kernel clean-kernel install-kernel
build-kernel:
	$(MAKE) -C $(SRCDIR)/kernel/

install-kernel:
	$(MAKE) -C $(SRCDIR)/kernel/ install

clean-kernel:
	$(MAKE) -C $(SRCDIR)/kernel/ clean

#
# initrd targets
#

.PHONY: build-initrd install-initrd clean-initrd
build-initrd: build-system
	$(MAKE) -C $(SRCDIR)/initrd/

install-initrd: build-initrd
	$(MAKE) -C $(SRCDIR)/initrd/ install

clean-initrd:
	$(MAKE) -C $(SRCDIR)/initrd/ clean

#
# system targets
#

.PHONY: build-system install-system clean-system
build-system: 
	$(MAKE) -C $(SRCDIR)/system/

install-system: build-system
	$(MAKE) -C $(SRCDIR)/system/ install

clean-system:
	$(MAKE) -C $(SRCDIR)/system/ clean

.PHONY: iso/kernel.elf.gz
iso/kernel.elf.gz: build-kernel
	$(BASH) -c "if [[ ! -e iso ]]; then mkdir iso; fi"
	$(CP) kernel/arch/$(ARCH)/kernel-$(VERSION).$(ARCH) iso/kernel.elf
	gzip -f iso/kernel.elf

.PHONY: iso/initrd.img.gz
iso/initrd.img.gz: build-initrd
	$(BASH) -c "if [[ ! -e iso ]]; then mkdir iso; fi"
	cp initrd/initrd.img iso/initrd.img
	gzip -f iso/initrd.img

.PHONY: try
try: aquila.iso
	qemu-kvm -cdrom aquila.iso -hda hd.img -serial stdio -m 2G -d cpu_reset -no-reboot -boot d

aquila.iso: iso/kernel.elf.gz iso/initrd.img.gz
	$(GRUB_MKRESCUE) -d /usr/lib/grub/i386-pc/ -o aquila.iso iso/
#	#$(GRUB_MKRESCUE) -d /usr/lib/grub/i386-pc/ --install-modules="multiboot normal videoinfo videotest gzio" --locales="en@quot" --fonts=ascii -o aquila.iso iso/
