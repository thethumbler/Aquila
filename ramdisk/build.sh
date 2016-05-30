if [[ ! -d out/bin ]]; then mkdir out/bin; fi
if [[ ! -d out/dev ]]; then mkdir out/dev; fi

for f in `ls *.c`; do gcc -m32 -nostdlib -ffreestanding -Wl,-Ttext=0x1000 \
`echo "-o out/bin/$f $f"  | sed 's/\..//'` -lgcc; done

cd out; find . | cpio -o > ../initrd.img; cd ..

