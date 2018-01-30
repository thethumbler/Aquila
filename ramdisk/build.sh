if [[ ! -d out/bin ]]; then mkdir out/bin; fi
if [[ ! -d out/dev ]]; then mkdir out/dev; fi
if [[ ! -d out/mnt ]]; then mkdir out/mnt; fi

for f in `ls *.c`; do ../build-tools/sys/bin/i686-aquila-gcc -m32 -I../build-tools/libc/sysroot/usr/include -nostdlib -ffreestanding -Wl,-Ttext=0x1000 \
`echo "-o out/$f $f"  | sed 's/\..//'` -lgcc ../build-tools/libc/sysroot/usr/lib/libc.a; done

cd src; sh build.sh; cd ..

cp etc out/ -r
cd out; find . | cpio -o > ../initrd.img; cd ..

