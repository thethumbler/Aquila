#
# Init
#

mkdir -p sys
mkdir -p libc/sysroot
mkdir -p pkgs
top_dir=$(pwd)
PERL_26=$(perl -e "if ($] gt '5.026000') { print 1 } else {print 0};")

#
# PKGS
#
BINUTILS="binutils-2.28"
GCC="gcc-7.3.0"
AUTOCONF="autoconf-2.69"
AUTOMAKE="automake-1.12.1"
NEWLIB="newlib-3.0.0"

pkgs_list=($BINUTILS $GCC  $AUTOCONF  $AUTOMAKE $NEWLIB);

build_dirs=($BINUTILS $AUTOCONF $AUTOMAKE $NEWLIB "build-newlib" "build-gcc");

#
# clean
#

if [ "$1" == "clean" ]
then
    echo "Okay I'll clean"
    for d in "${build_dirs[@]}"
    do
	make clean -C $d;
    done
    echo "cleaned successfuly"
    exit
fi;

#
# distclean
#
if [ "$1" == "distclean" ]
   then
       echo "Okay I'll remove everything"
       for d in "${build_dirs[@]}"
       do
	   rm -rf $d;
       done
       #there are common folder in both list but for future change
       for d in "${pkgs_list[@]}"
       do
	   rm -rf $d;
       done
       echo "Done.."
       exit;
fi



set -e #don't continue if a command return non zero

#
#Download Packages
#

echo "Downloading Packages";

if [[ ! -f "pkgs/$BINUTILS.tar.gz" ]]; then
    wget -P pkgs https://ftp.gnu.org/gnu/binutils/binutils-2.28.tar.gz;
fi;

if [[ ! -f "pkgs/$GCC.tar.gz" ]]; then
    wget -P pkgs/ https://ftp.gnu.org/gnu/gcc/gcc-7.3.0/gcc-7.3.0.tar.gz;
fi;

if [[ ! -f "pkgs/$AUTOCONF.tar.gz" ]]; then
    wget -P pkgs/ "https://ftp.gnu.org/gnu/autoconf/$AUTOCONF.tar.gz";
fi;

if [[ ! -f "pkgs/$AUTOMAKE.tar.gz" ]]; then
    wget -P pkgs/ "https://ftp.gnu.org/gnu/automake/$AUTOMAKE.tar.gz";
fi;

if [[ ! -f "pkgs/$NEWLIB.tar.gz" ]]; then
    wget -P pkgs/ "ftp://sourceware.org/pub/newlib/$NEWLIB.tar.gz";
fi;

echo "The packages are here now"

#
# Uncompressing
#

echo "Hey packages!, Make yourself home"
echo "Uncompressing & Configuring.."


if [[ ! -d $AUTOCONF ]]; then
    tar xzf "pkgs/$AUTOCONF.tar.gz";
    cd $AUTOCONF && ./configure --prefix=$top_dir/sys
    cd ..
fi

if [[ ! -d $AUTOMAKE ]]; then
    tar xzf "pkgs/$AUTOMAKE.tar.gz";
    cd $AUTOMAKE;
    if [ $PERL_26 -eq 1 ] #patch if version > 26
    then
	patch -N < ../patches/automake.patch
    fi
    ./configure --prefix=$top_dir/sys
    cd ..
fi

if [[ ! -d $BINUTILS ]]; then
    tar xzf "pkgs/$BINUTILS.tar.gz";
    cd $BINUTILS
    ./configure --prefix=$top_dir/sys --target=i686-elf --disable-nls --disable-werror --with-sysroot;
    cd ..
fi

if [[ ! -d "build-gcc" ]]; then
    if [[ ! -d $GCC ]]; then
	tar xzf "pkgs/$GCC.tar.gz";
    fi;
    cd "$GCC";
    ./contrib/download_prerequisites;
    cd ..;
    mkdir -p "build-gcc" && cd "build-gcc";
    ../$GCC/configure --prefix=$top_dir/sys --target=i686-elf --disable-nls --enable-languages=c,c++ --without-headers;
    cd ..;
fi;

if [[ ! -d $NEWLIB ]]; then
    tar xzf "pkgs/$NEWLIB.tar.gz";
    cp aquila $NEWLIB/newlib/libc/sys/ -r;
    cd $NEWLIB;
    patch -p1 < ../patches/newlib.patch;
    cd ..
fi;

echo "Everything is ready.."

#
#Building
#

echo "Let's start Building ...."

echo "Building binutils ...."
cd $BINUTILS;
make -j $(nproc) && make install;
cd ..;
echo "Done..."

echo "Building gcc..."
cd "build-gcc"
make -j $(nproc) all-gcc;
make -j $(nproc) all-target-libgcc;
make -j $(nproc) install-gcc;
make -j $(nproc) install-target-libgcc;
cd ..;
echo "Done.."

echo "Building autoconf.."
cd $AUTOCONF && make -j $(nproc) && make install;
cd ..;
echo "Done.."

echo "Building automake.."
cd $AUTOMAKE;
make -j $(nproc) && make install;
cd ..;
echo "Done.."

echo "Building newlib.."

cd $NEWLIB;
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
../$NEWLIB/configure --prefix=/usr --target=i686-aquila;
make -j $(nproc) CFLAGS=-march=i586 all;
make DESTDIR=$top_dir/libc/sysroot install;
cd ..;
cp -ar $top_dir/libc/sysroot/usr/i686-aquila/* $top_dir/libc/sysroot/usr/
echo "Done.."

echo "The build was successful, you can now build the image"
