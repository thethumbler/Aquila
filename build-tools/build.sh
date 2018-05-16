#
# Init
#

set -e #don't continue if a command return non zero
mkdir -p sys
mkdir -p libc/sysroot
mkdir -p pkgs
top_dir=$(pwd)
PERL_26=$(perl -e "if ($] gt '5.026000') { print 1 } else {print 0};")

#
# Cross binutils and GCC
#

if [[ ! -f "pkgs/binutils-2.28.tar.gz" ]]; then
	wget https://ftp.gnu.org/gnu/binutils/binutils-2.28.tar.gz;
    mv "binutils-2.28.tar.gz" "pkgs/binutils-2.28.tar.gz";
    rm -rf "binutils-2.28";
fi;

if [[ ! -f "pkgs/gcc-7.3.0.tar.gz" ]]; then
	wget https://ftp.gnu.org/gnu/gcc/gcc-7.3.0/gcc-7.3.0.tar.gz;
    mv "gcc-7.3.0.tar.gz" "pkgs/gcc-7.3.0.tar.gz";
    rm -rf "gcc-7.3.0";
fi;

if [[ ! -d binutils-2.28 ]]; then
	tar xzf "pkgs/binutils-2.28.tar.gz";
    cd "binutils-2.28";
    ./configure --prefix=$top_dir/sys --target=i686-elf --disable-nls --disable-werror --with-sysroot;
    make -j $(nproc) && make install;
    cd ..;
fi;


if [[ ! -d gcc-7.3.0 ]]; then
	tar xzf "pkgs/gcc-7.3.0.tar.gz";
    cd "gcc-7.3.0";
    ./contrib/download_prerequisites;
    cd ..;
    mkdir -p "build-gcc" && cd "build-gcc";
    ../gcc-7.3.0/configure --prefix=$top_dir/sys --target=i686-elf --disable-nls --enable-languages=c,c++ --without-headers;
    make -j $(nproc) all-gcc;
    make -j $(nproc) all-target-libgcc;
    make -j $(nproc) install-gcc;
    make -j $(nproc) install-target-libgcc;
    cd ..;
fi;

#
#   autoconf and automake
#

# Fetch autoconf and automake if not present
if [[ ! -f "pkgs/autoconf-2.69.tar.gz" ]]; then
    wget "https://ftp.gnu.org/gnu/autoconf/autoconf-2.69.tar.gz";
    mv "autoconf-2.69.tar.gz" "pkgs/autoconf-2.69.tar.gz";
    rm -rf "autoconf-2.69";
	tar xzf "pkgs/autoconf-2.69.tar.gz";
    cd autoconf-2.69 && ./configure --prefix=$top_dir/sys && make -j $(nproc) && make install;
    cd ..;
fi;

if [[ ! -f "pkgs/automake-1.12.1.tar.gz" ]]; then
    wget "https://ftp.gnu.org/gnu/automake/automake-1.12.1.tar.gz";
    mv "automake-1.12.1.tar.gz" "pkgs/automake-1.12.1.tar.gz";
    rm -rf "automake-1.12.1";
    tar xzf "pkgs/automake-1.12.1.tar.gz";
    cd automake-1.12.1;
    
    if [ $PERL_26 -eq 1 ] #patch if version > 26
    then
	patch < ../patches/automake.patch;
    fi
    
    ./configure --prefix=$top_dir/sys && make -j $(nproc) && make install;
    cd ..;
fi;

# newlib
# Fetch & patch
if [[ ! -f "pkgs/newlib-3.0.0.tar.gz" ]]; then
    wget -O "pkgs/newlib-3.0.0.tar.gz" "ftp://sourceware.org/pub/newlib/newlib-3.0.0.tar.gz";
fi;

# Always rebuild newlib
rm -rf "newlib-3.0.0";
tar -xzf "pkgs/newlib-3.0.0.tar.gz";
cp aquila newlib-3.0.0/newlib/libc/sys/ -r;
cd newlib-3.0.0;
patch -p1 < ../patches/newlib.patch;
cd newlib/libc/sys/aquila;

cd pthread; $top_dir/sys/bin/aclocal -I ../../../..;
$top_dir/sys/bin/automake --cygnus Makefile;
$top_dir/sys/bin/autoconf;
cd ..;

$top_dir/sys/bin/aclocal -I ../../..;
$top_dir/sys/bin/automake --cygnus Makefile;
cd ..;
$top_dir/sys/bin/autoconf;
cd aquila;
$top_dir/sys/bin/autoconf;
cd $top_dir;

# Link binaries
ln -f sys/bin/i686-elf-ar sys/bin/i686-aquila-ar
ln -f sys/bin/i686-elf-as sys/bin/i686-aquila-as
ln -f sys/bin/i686-elf-gcc sys/bin/i686-aquila-gcc
ln -f sys/bin/i686-elf-gcc sys/bin/i686-aquila-cc
ln -f sys/bin/i686-elf-ranlib sys/bin/i686-aquila-ranlib
export PATH=$top_dir/sys/bin:$PATH;
rm -rf build-newlib && mkdir -p build-newlib && cd build-newlib;
../newlib-3.0.0/configure --prefix=/usr --target=i686-aquila;
make -j $(nproc) CFLAGS=-march=i586 all;
make DESTDIR=$top_dir/libc/sysroot install;
cd ..;
cp -ar $top_dir/libc/sysroot/usr/i686-aquila/* $top_dir/libc/sysroot/usr/

echo "The build was successful, you can now build the image"
