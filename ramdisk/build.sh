if [[ ! -d out/bin ]]; then mkdir out/bin; fi
if [[ ! -d out/dev ]]; then mkdir out/dev; fi

for f in `ls *.c`; do /opt/cross/bin/i686-aquila-gcc -m32 -I./usr/include -nostdlib -ffreestanding -Wl,-Ttext=0x1000 \
`echo "-o out/$f $f"  | sed 's/\..//'` -lgcc ./usr/lib/libc.a; done

cd src; sh build.sh; cd ..

cp etc out/ -r
cd out; find . | cpio -o > ../initrd.img; cd ..

