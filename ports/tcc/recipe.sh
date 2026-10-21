# TinyCC, a small C compiler, assembler and linker, https://bellard.org/tcc/
# (from git: the 0.9.27 release is from 2017, pinned to a commit of mob;
# GitHub's mirror of repo.or.cz/tinycc.git, which often drops connections,
# the commit hash pins the sources either way)
#
# It compiles on AquilaOS against the C library, which this port installs
# too: newlib's headers, startup files and archives from the sysroot.

NAME=tcc
VERSION=0.9.28rc
GIT=https://github.com/TinyCC/tinycc.git
COMMIT=e23dabed154a8495f3ac1ff3cc9d04fa1c207893

build()
{
    # a native compiler for AquilaOS, built by the cross compiler: static
    # executables by default (no dynamic linking), no dlopen() and no
    # threads; no -run, no bounds checker and backtraces (no-run.patch)
    # (the code it makes is the system's, $ARCH's: i386 or x86_64)
    ./configure --prefix=/usr --cpu="$ARCH" --cross-prefix="$TARGET-" --cc=gcc --ar=ar \
        --sysincludepaths=/usr/lib/tcc/include:/usr/include \
        --libpaths={B}:/usr/lib --crtprefix=/usr/lib --tcc-switches=-static \
        --config-backtrace=no --config-bcheck=no --config-semlock=no \
        --extra-cflags="-O2 -DCONFIG_TCC_STATIC -DCONFIG_TCC_NO_RUN"

    # c2str generates a header while building, on the build host
    cc -DC2STR conftest.c -o c2str.exe

    # its library of helper functions, compiled by the cross compiler: tcc
    # itself can't run here
    # (the sources are in AquilaOS's tree, its git commit isn't tcc's)
    GIT_DIR=/nonexistent make -j"$JOBS" tcc LIBS=-lm
    make libtcc1.a LIBS=-lm "$ARCH-libtcc1-usegcc=yes"

    # gcc built the C library, which calls its helper functions (libgcc,
    # e.g. __divmoddi4): they are added after tcc's own, which tcc links
    # last (the GCC Runtime Library Exception allows it)
    mkdir libgcc
    (cd libgcc && $AR x "$($CC -print-libgcc-file-name)")
    $AR q libtcc1.a libgcc/*.o
    $RANLIB libtcc1.a

    install -d "$DESTDIR/usr/bin" "$DESTDIR/usr/lib/tcc/include"
    install -m 755 tcc "$DESTDIR/usr/bin/tcc"
    $STRIP "$DESTDIR/usr/bin/tcc"
    ln -sf tcc "$DESTDIR/usr/bin/cc"
    install -m 644 libtcc1.a "$DESTDIR/usr/lib/tcc/"
    install -m 644 include/*.h "$DESTDIR/usr/lib/tcc/include/"

    # the C library: headers (with the kernel's, include/uapi), startup
    # files and archives; tcc's name for the startup file is crt1.o
    local sysroot
    sysroot=$($CC -print-sysroot)

    cp -r "$sysroot/usr/include/." "$DESTDIR/usr/include/"
    for f in crt0.o crti.o crtn.o libc.a libm.a; do
        install -m 644 "$sysroot/usr/lib/$f" "$DESTDIR/usr/lib/$f"
    done
    ln -sf crt0.o "$DESTDIR/usr/lib/crt1.o"
    $STRIP -g "$DESTDIR/usr/lib/libc.a" "$DESTDIR/usr/lib/libm.a"
}
