# This is the top leve Makefile of AquilaOS
#
# Targets:
#
# kernel	- Build kernel image in kernel/{ARCH}/kernel-{VERSION}.{ARCH}
# initrd	- Build ramdisk image in initrd/initrd.img
#

export

ARCH=i386
VERSION= 0.0.1
CP = cp
BASH = bash

GRUB_MKRESCUE = $(shell command -v grub2-mkrescue 2> /dev/null)
ifeq ($(GRUB_MKRESCUE),)
GRUB_MKRESCUE = grub-mkrescue
endif

SRC_DIR := $(TRAVIS_BUILD_DIR)

ifeq ($(SRC_DIR),)
SRC_DIR != pwd
endif

KERNEL_CONFIG := $(CONFIG)
ifeq ($(KERNEL_CONFIG),)
KERNEL_CONFIG := i386-pc
endif

KERNEL_MAKE_FLAGS := CONFIG=$(KERNEL_CONFIG)

aquila.iso: iso/kernel.elf.gz iso/initrd.img.gz
	$(GRUB_MKRESCUE) -d /usr/lib/grub/i386-pc/ --install-modules="multiboot normal videoinfo videotest gzio" --locales="en@quot" --fonts=ascii -o aquila.iso iso/

.PHONY: kernel initrd system
kernel:
	$(MAKE) $(KERNEL_MAKE_FLAGS) -C kernel/

initrd: iso/initrd.img
system:
	$(MAKE) -C system/

.PHONY: iso/kernel.elf
iso/kernel.elf.gz: kernel kernel/arch/$(ARCH)/kernel.elf
	$(BASH) -c "if [[ ! -e iso ]]; then mkdir iso; fi"
	$(CP) kernel/arch/$(ARCH)/kernel-$(VERSION).$(ARCH) iso/kernel.elf
	gzip -f iso/kernel.elf

.PHONY: %$(ARCH)/kernel.elf
%$(ARCH)/kernel.elf:
	$(MAKE) $(KERNEL_MAKE_FLAGS) -C kernel/

iso/initrd.img.gz: initrd/initrd.img
	cp initrd/initrd.img iso/initrd.img
	gzip -f iso/initrd.img

initrd/initrd.img: system
	$(MAKE) -C initrd/

try: aquila.iso
	qemu-kvm -cdrom aquila.iso -hda hd.img -serial stdio -m 1G -d cpu_reset -no-reboot -boot d

.PHONY: clean
clean:
	$(MAKE) $(KERNEL_MAKE_FLAGS) clean -C kernel
	$(MAKE) clean -C system
	$(MAKE) clean -C initrd
	$(RM) -f iso/kernel.elf.gz iso/initrd.img.gz aquila.iso

.PHONY: distclean
distclean:
	$(MAKE) clean
	$(MAKE) $(KERNEL_MAKE_FLAGS) distclean -C kernel

.PHONY: src.tgz
SRC := initrd/ kernel/ lib/ libexec/ LICENSE loader/ Makefile README.md system/
src.tgz:
	make distclean
	tar czf $@ $(SRC)

