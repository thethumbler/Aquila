# libfontenc, font encodings for libXfont, https://www.x.org/
# (only the library, for other ports: installed into $STAGING)

NAME=libfontenc
VERSION=1.1.8
URL=https://www.x.org/archive/individual/lib/libfontenc-$VERSION.tar.xz
SHA256=7b02c3d405236e0d86806b1de9d6868fe60c313628b38350b032914aa4fd14c6
DEPENDS="zlib xorgproto"

build()
{
    aquila_config_sub

    ./configure --host=i686-aquila --prefix=/usr --disable-shared \
        --with-encodingsdir=/usr/share/fonts/X11/encodings
    make -j"$JOBS"
    make DESTDIR="$STAGING" install
}
