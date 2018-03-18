#
# Init
#

mkdir -p sys
mkdir -p libc/sysroot
top_dir=$(pwd)

#
# Cross binutils and GCC
#

if [[ ! -f binutils-2.28.tar.gz ]]; then
	wget https://ftp.gnu.org/gnu/binutils/binutils-2.28.tar.gz;
fi;

if [[ ! -f gcc-7.3.0.tar.gz ]]; then
	wget https://ftp.gnu.org/gnu/gcc/gcc-7.3.0/gcc-7.3.0.tar.gz;
fi;

if [[ ! -d binutils-2.28 ]]; then
	tar xzf binutils-2.28.tar.gz;
    cd binutils-2.28;
    ./configure --prefix=$top_dir/sys --target=i686-elf --disable-nls --disable-werror --with-sysroot;
    make && make install;
    cd ..;
fi;


if [[ ! -d gcc-7.3.0 ]]; then
	tar xzf gcc-7.3.0.tar.gz;
    cd gcc-7.3.0;
    ./contrib/download_prerequisites;
    cd ..;
    mkdir -p build-gcc && cd build-gcc;
    ../gcc-7.3.0/configure --prefix=$top_dir/sys --target=i686-elf --disable-nls --enable-languages=c,c++ --without-headers;
    make all-gcc;
    make all-target-libgcc;
    make install-gcc;
    make install-target-libgcc;
    cd ..;
fi;

#
#   autoconf and automake
#

# Fetch autoconf and automake if not present
#if [[ ! -f autoconf-2.65.tar.gz ]]; then
if [[ ! -f autoconf-2.69.tar.gz ]]; then
    wget https://ftp.gnu.org/gnu/autoconf/autoconf-2.69.tar.gz;
fi;

if [[ ! -f automake-1.12.1.tar.gz ]]; then
	wget https://ftp.gnu.org/gnu/automake/automake-1.12.1.tar.gz;
fi;

# Unpack and build

#if [[ ! -d autoconf-2.65 ]]; then
if [[ ! -d autoconf-2.69 ]]; then
	tar xzf autoconf-2.69.tar.gz;
    cd autoconf-2.69 && ./configure --prefix=$top_dir/sys && make && make install;
    cd ..;
fi;


if [[ ! -d automake-1.12.1 ]]; then
	tar xzf automake-1.12.1.tar.gz;
    cd automake-1.12.1 && ./configure --prefix=$top_dir/sys && make && make install;
    cd ..;
fi;

# newlib
# Fetch
if [[ ! -f "newlib-3.0.0.tar.gz" ]]; then
    wget "ftp://sourceware.org/pub/newlib/newlib-3.0.0.tar.gz";
fi;


# Patch & Build
if [[ ! -d newlib-3.0.0 ]]; then
    tar -xzf newlib-3.0.0.tar.gz;
    cp aquila newlib-3.0.0/newlib/libc/sys/ -r;
    cd newlib-3.0.0;
    patch -p1 < ../patches/newlib.patch;
    cd newlib/libc/sys/aquila;
    $top_dir/sys/bin/aclocal -I ../../..;
    $top_dir/sys/bin/automake --cygnus Makefile;
    cd ..;
    $top_dir/sys/bin/autoconf;
    cd aquila;
    $top_dir/sys/bin/autoconf;
    cd $top_dir;

    # Link binaries
    ln sys/bin/i686-elf-ar sys/bin/i686-aquila-ar
    ln sys/bin/i686-elf-as sys/bin/i686-aquila-as
    ln sys/bin/i686-elf-gcc sys/bin/i686-aquila-gcc
    ln sys/bin/i686-elf-gcc sys/bin/i686-aquila-cc
    ln sys/bin/i686-elf-ranlib sys/bin/i686-aquila-ranlib
fi;

# Always rebuild newlib
export PATH=$top_dir/sys/bin:$PATH;
rm -rf build-newlib && mkdir -p build-newlib && cd build-newlib;
../newlib-3.0.0/configure --prefix=/usr --target=i686-aquila;
make all;
make DESTDIR=$top_dir/libc/sysroot install;
cd ..;
cp -ar $top_dir/libc/sysroot/usr/i686-aquila/* $top_dir/libc/sysroot/usr/
