gcc -m32 -nostdlib -ffreestanding -Wl,-Tlink.ld -Wall -Werror \
boot/boot.o cpu/cpu.o console/console.o mem/mem.o ../../core/core.o \
../../devices/devices.o ../../fs/fs.o ../../sys/sys.o -lgcc -o iso/kernel.elf

for f in `ls ramdisk_src`; do gcc -m32 -nostdlib -ffreestanding -Wl,-Ttext=0x1000 \
`echo "-o ramdisk/bin/$f ramdisk_src/$f"  | sed 's/\..//'`; done

cd ramdisk; find . | cpio -o > ../iso/initrd.img; cd ..
grub2-mkrescue -o cd.iso iso/
qemu-kvm -cdrom cd.iso -serial stdio -m 256M
