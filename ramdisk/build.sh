#!bash
if [[ ! -d out/bin ]]; then mkdir -p out/bin; fi
if [[ ! -d out/sbin ]]; then mkdir -p out/sbin; fi
if [[ ! -d out/dev ]]; then mkdir -p out/dev; fi
if [[ ! -d out/mnt ]]; then mkdir -p out/mnt; fi
if [[ ! -d out/proc ]]; then mkdir -p out/proc; fi
if [[ ! -d out/tmp ]]; then mkdir -p out/tmp; fi
if [[ ! -d out/root ]]; then mkdir -p out/root; fi
if [[ ! -d out/home/live ]]; then mkdir -p out/home/live; fi

for f in `ls *.c`; do ../build-tools/sys/bin/i686-aquila-gcc -m32 -I../build-tools/libc/sysroot/usr/include -nostdlib -ffreestanding -Wl,-Ttext=0x1000 \
`echo "-o out/$f $f"  | sed 's/\..//'` -lgcc ../build-tools/libc/sysroot/usr/lib/libc.a; done

cp etc out/ -r

cp init.rc out/
cp ../system/aqbox/aqbox  out/bin/

# symlink binaries
ln -s /bin/aqbox out/bin/cat
ln -s /bin/aqbox out/bin/clear
ln -s /bin/aqbox out/bin/echo
ln -s /bin/aqbox out/bin/env
ln -s /bin/aqbox out/bin/ls
ln -s /bin/aqbox out/bin/mkdir
ln -s /bin/aqbox out/bin/mknod
ln -s /bin/aqbox out/bin/ps
ln -s /bin/aqbox out/bin/pwd
ln -s /bin/aqbox out/bin/sh
ln -s /bin/aqbox out/bin/stat
ln -s /bin/aqbox out/bin/uname
ln -s /bin/aqbox out/bin/unlink

# symlink superuser binaries
ln -s /bin/aqbox out/sbin/login
ln -s /bin/aqbox out/sbin/mount
ln -s /bin/aqbox out/sbin/kbd

cp ../system/fbterm/fbterm out/bin/
cp ../system/nuklear/nuklear out/bin/
cp ../system/kilo/kilo out/bin/

cp dist/* out/ -r

cd out; find . | cpio -o > ../initrd.img; cd ..

