for f in `ls *.c`; do /opt/cross/bin/i686-aquila-gcc -m32 -I../usr/include -nostdlib -ffreestanding -Wl,-Ttext=0x1000 \
`echo "-o ../out/bin/$f $f"  | sed 's/\.c//'` ../usr/lib/crt0.o -lgcc ../usr/lib/libc.a ; done
