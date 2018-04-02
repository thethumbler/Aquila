#!bash
if [[ ! -d out/bin ]]; then mkdir -p out/bin; fi
if [[ ! -d out/dev ]]; then mkdir -p out/dev; fi
if [[ ! -d out/mnt ]]; then mkdir -p out/mnt; fi
if [[ ! -d out/proc ]]; then mkdir -p out/proc; fi
if [[ ! -d out/tmp ]]; then mkdir -p out/tmp; fi

for f in `ls *.c`; do ../build-tools/sys/bin/i686-aquila-gcc -m32 -I../build-tools/libc/sysroot/usr/include -nostdlib -ffreestanding -Wl,-Ttext=0x1000 \
`echo "-o out/$f $f"  | sed 's/\..//'` -lgcc ../build-tools/libc/sysroot/usr/lib/libc.a; done

cd src; bash build.sh; cd ..

cp etc out/ -r
#cp sysroot/usr out/ -r

cp initrc out/
cp ../system/aqbox/aqbox  out/bin/
cp ../system/fbterm/fbterm out/bin/
cd out; find . | cpio -o > ../initrd.img; cd ..

