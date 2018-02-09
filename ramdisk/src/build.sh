#!bash
for f in `ls *.c`; do ../../build-tools/sys/bin/i686-aquila-gcc -m32 -I../../build-tools/libc/sysroot/usr/include -nostdlib -ffreestanding -Wl,-Ttext=0x1000 \
`echo "-o ../out/bin/$f $f"  | sed 's/\.c//'` ../../build-tools/libc/sysroot/usr/lib/crt0.o -lgcc ../../build-tools/libc/sysroot/usr/lib/libc.a ; done
