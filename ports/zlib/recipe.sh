# zlib, compression library, https://zlib.net/
# (only the library, for other ports: installed into $STAGING)

NAME=zlib
VERSION=1.3.1
URL=https://github.com/madler/zlib/releases/download/v$VERSION/zlib-$VERSION.tar.gz
SHA256=9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23

build()
{
    CHOST=i686-aquila ./configure --prefix=/usr --static
    make -j"$JOBS" libz.a
    make DESTDIR="$STAGING" install
}
