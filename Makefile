export

#ARCH = x86_64
ARCH = x86
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
KERNEL_CONFIG := i686-pc
#KERNEL_CONFIG := x86_64-pc
endif

KERNEL_MAKE_FLAGS := CONFIG=$(KERNEL_CONFIG)

aquila.iso: kernel initrd
	#$(GRUB_MKRESCUE) -d /usr/lib/grub/i386-pc/ -o aquila.iso iso/
	$(GRUB_MKRESCUE) -d /usr/lib/grub/i386-pc/ --install-modules="multiboot normal" --locales="en@quot" --fonts=ascii -o aquila.iso iso/

.PHONY: kernel initrd system
kernel: iso/kernel.elf
initrd: iso/initrd.img
system:
	$(MAKE) -C system/

.PHONY: iso/kernel.elf
iso/kernel.elf: kernel/arch/$(ARCH)/kernel.elf
	$(MAKE) $(KERNEL_MAKE_FLAGS) -C kernel/
	$(BASH) -c "if [[ ! -e iso ]]; then mkdir iso; fi"
	$(CP) kernel/arch/$(ARCH)/kernel.elf $@

.PHONY: %$(ARCH)/kernel.elf
%$(ARCH)/kernel.elf:
	$(MAKE) $(KERNEL_MAKE_FLAGS) -C kernel/

iso/initrd.img: initrd/initrd.img
	cp initrd/initrd.img iso/initrd.img

initrd/initrd.img: system
	$(MAKE) -C initrd/

try: aquila.iso
	qemu-kvm -fda floppy.img -cdrom aquila.iso -hda hd.img -serial stdio -m 1G -d cpu_reset -no-reboot -boot d

.PHONY: clean
clean:
	$(MAKE) $(KERNEL_MAKE_FLAGS) clean -C kernel
	$(MAKE) clean -C system
	$(MAKE) clean -C initrd
	$(RM) -f iso/kernel.elf iso/initrd.img aquila.iso

.PHONY: distclean
distclean:
	$(MAKE) clean
	$(MAKE) $(KERNEL_MAKE_FLAGS) distclean -C kernel
