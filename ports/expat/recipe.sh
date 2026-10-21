# Expat, an XML parser (for fontconfig), https://libexpat.github.io/
# (only the library, for other ports: installed into $STAGING)

NAME=expat
VERSION=2.6.4
URL=https://github.com/libexpat/libexpat/releases/download/R_2_6_4/expat-$VERSION.tar.xz
SHA256=a695629dae047055b37d50a0ff4776d1d45d0a4c842cf4ccee158441f55ff7ee

build()
{
    aquila_config_sub

    ./configure --host=i686-aquila --prefix=/usr --disable-shared \
        --without-docbook --without-examples --without-tests --without-xmlwf
    make -j"$JOBS"
    make DESTDIR="$STAGING" install
}
