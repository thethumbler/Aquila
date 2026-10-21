# Mbed TLS, TLS and the cryptography under it (for curl), https://www.trustedfirmware.org/projects/mbed-tls/
# (only the libraries, for other ports: installed into $STAGING)
#
# Its entropy is the kernel's (/dev/urandom); without its own sockets
# and timers (curl has its own)

NAME=mbedtls
VERSION=3.6.7
URL=https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-$VERSION/mbedtls-$VERSION.tar.bz2
SHA256=a7e8bcbec0e6f761b4af24f25677626b35f762f68eef79c08677a363212d11f6

build()
{
    python3 scripts/config.py unset MBEDTLS_NET_C
    python3 scripts/config.py unset MBEDTLS_TIMING_C

    make -j"$JOBS" -C library CC="$CC" AR="$AR" CFLAGS="-O2" \
        libmbedcrypto.a libmbedx509.a libmbedtls.a

    install -d "$STAGING/usr/include" "$STAGING/usr/lib"
    cp -r include/mbedtls include/psa "$STAGING/usr/include/"
    install -m 644 library/libmbedcrypto.a library/libmbedx509.a library/libmbedtls.a \
        "$STAGING/usr/lib/"
}
