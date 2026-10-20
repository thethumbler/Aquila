# dash, the Debian Almquist shell, http://gondor.apana.org.au/~herbert/dash/
# (fetched from Debian, whose orig tarball is the upstream release)

NAME=dash
VERSION=0.5.13.5
URL=https://deb.debian.org/debian/pool/main/d/dash/dash_$VERSION.orig.tar.gz
SHA256=40090101a2a491f13e901d3d48e90414f26634628b9bfff35ff540363c227a7d

build()
{
    # mksignames runs on the build host and would use its signal numbers,
    # give it a <signal.h> with AquilaOS's numbers instead
    mkdir -p aquila-signal
    {
        echo '#include_next <signal.h>'
        echo '#include <signal.h>' | cc -dM -E -x c - |
            awk '/^#define SIG[A-Z0-9]+ / { print "#undef", $2 }'
        echo '#undef NSIG'
        echo '#undef sa_handler'
        echo '#undef sa_sigaction'
        echo '#include <aquila/signal.h>'
    } > aquila-signal/signal.h

    ./configure --host=i686-aquila --prefix=/ \
        CC_FOR_BUILD="cc -w -I$PWD/aquila-signal -I$AQUILA_UAPI"
    make -j"$JOBS"

    install -d "$DESTDIR/bin"
    install -m 755 src/dash "$DESTDIR/bin/dash"

    # the system shell, replacing aqbox's sh
    ln -sf dash "$DESTDIR/bin/sh"
}
