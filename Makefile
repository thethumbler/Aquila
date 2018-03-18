ARCH = x86
CP = cp
BASH = bash

GRUB_MKRESCUE = $(shell command -v grub2-mkrescue 2> /dev/null)
ifeq ($(GRUB_MKRESCUE),)
GRUB_MKRESCUE = grub-mkrescue
endif

aquila.iso: kernel ramdisk
	$(GRUB_MKRESCUE) -o aquila.iso iso/

.PHONY: kernel ramdisk system
kernel: iso/kernel.elf
ramdisk: iso/initrd.img
system:
	$(RM) -rf system/usr
	$(CP) build-tools/libc/sysroot/usr system/usr -r
	$(MAKE) -C system/

.PHONY: iso/kernel.elf
iso/kernel.elf: kernel/arch/$(ARCH)/kernel.elf
	$(MAKE) -C kernel/
	$(BASH) -c "if [[ ! -e iso ]]; then mkdir iso; fi"
	$(CP) kernel/arch/$(ARCH)/kernel.elf $@

.PHONY: %$(ARCH)/kernel.elf
%$(ARCH)/kernel.elf:
	$(MAKE) -C kernel/

iso/initrd.img: ramdisk/initrd.img
	cp ramdisk/initrd.img iso/initrd.img

ramdisk/initrd.img: system
	cd ramdisk; $(BASH) build.sh

try: aquila.iso
	qemu-kvm -cdrom aquila.iso -serial stdio -m 1G -d cpu_reset -no-reboot -hda hd.img -boot d

.PHONY: clean
clean:
	$(MAKE) clean -C kernel
	$(MAKE) clean -C system
	$(RM) -f ramdisk/out/* -r
	$(RM) -f ramdisk/initrd.img
	$(RM) -f iso/kernel.elf iso/initrd.img aquila.iso

.PHONY: distclean
distclean:
	$(MAKE) clean
	$(MAKE) distclean -C kernel
