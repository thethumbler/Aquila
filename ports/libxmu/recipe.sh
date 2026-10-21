# libXmu, miscellaneous utilities of X clients, https://www.x.org/
# (only the library, for other ports: installed into $STAGING)

NAME=libxmu
VERSION=1.2.1
URL=https://www.x.org/archive/individual/lib/libXmu-$VERSION.tar.xz
SHA256=fcb27793248a39e5fcc5b9c4aec40cc0734b3ca76aac3d7d1c264e7f7e14e8b2
ARCHIVE_DIR=libXmu-$VERSION
DEPENDS="libxt libxext"

build()
{
    aquila_config_sub

    ./configure --host=i686-aquila --prefix=/usr --disable-shared \
        --disable-docs --without-xmlto --without-fop --disable-ipv6
    make -j"$JOBS"
    make DESTDIR="$STAGING" install
}
