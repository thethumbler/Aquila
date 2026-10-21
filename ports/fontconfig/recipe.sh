# fontconfig, which finds the fonts programs ask for by name (Xft),
# https://www.freedesktop.org/wiki/Software/fontconfig/
# (the library into $STAGING; its configuration, /etc/fonts, and fc-cache,
# fc-list and fc-match on the system)

NAME=fontconfig
VERSION=2.15.0
URL=https://www.freedesktop.org/software/fontconfig/release/fontconfig-$VERSION.tar.xz
SHA256=63a0658d0e06e0fa886106452b58ef04f21f58202ea02a94c39de0d3335d7c0e
DEPENDS="freetype expat"

build()
{
    aquila_config_sub

    # the fonts in /usr/share/fonts, the cache in /var/cache/fontconfig (made
    # on AquilaOS, fc-cache); newlib has random() but not initstate(), its
    # random numbers are lrand48()'s
    ac_cv_func_random=no ./configure --host=i686-aquila --prefix=/usr --sysconfdir=/etc \
        --localstatedir=/var --disable-shared --disable-docs \
        --disable-cache-build --with-default-fonts=/usr/share/fonts
    make -j"$JOBS"
    make DESTDIR="$STAGING" install

    # the configuration, conf.d links to conf.avail; the cache directory,
    # which the first program to look for fonts fills
    install -d "$DESTDIR/usr/bin" "$DESTDIR/etc" "$DESTDIR/usr/share" \
        "$DESTDIR/var/cache/fontconfig"
    cp -r "$STAGING/etc/fonts" "$DESTDIR/etc/"
    cp -r "$STAGING/usr/share/fontconfig" "$DESTDIR/usr/share/"
    for t in fc-cache fc-list fc-match; do
        install -m 755 "$STAGING/usr/bin/$t" "$DESTDIR/usr/bin/$t"
        $STRIP "$DESTDIR/usr/bin/$t"
    done
}
