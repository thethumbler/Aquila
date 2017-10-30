ARCH = x86
CP = cp

cd.iso: kernel ramdisk
	grub2-mkrescue -o cd.iso iso/

.PHONY: kernel ramdisk
kernel: iso/kernel.elf
ramdisk: iso/initrd.img

.PHONY: iso/kernel.elf
iso/kernel.elf: kernel/arch/$(ARCH)/kernel.elf
	$(MAKE) -C kernel/
	if [[ ! -e iso ]]; then mkdir iso; fi
	$(CP) kernel/arch/$(ARCH)/kernel.elf $@

.PHONY: %$(ARCH)/kernel.elf
%$(ARCH)/kernel.elf:
	$(MAKE) -C kernel/

iso/initrd.img: ramdisk/initrd.img
	cp ramdisk/initrd.img iso/initrd.img

ramdisk/initrd.img:
	cd ramdisk; sh build.sh

try: cd.iso
	#qemu-kvm -cdrom cd.iso -serial stdio -m 256M -smp 4
	qemu-kvm -cdrom cd.iso -serial stdio -m 1G -d cpu_reset -no-reboot -hda hd.img -boot d

.PHONY: clean
clean:
	$(MAKE) clean -C kernel
	$(RM) -f ramdisk/out/* -r
	$(RM) -f ramdisk/initrd.img
	$(RM) -f iso/kernel.elf iso/initrd.img cd.iso

.PHONY: distclean
distclean:
	$(MAKE) clean
	$(MAKE) distclean -C kernel
