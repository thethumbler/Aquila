# curl, transfers with URLs (HTTP and HTTPS, FTP, ...), https://curl.se/
# (`curl` in /usr/bin)
#
# TLS through Mbed TLS, the servers verified with the CA certificates of
# ca-certificates; names resolved one at a time (no resolver thread); no
# LDAP, HTTP/2, IDN or public suffix list

NAME=curl
VERSION=8.23.0
URL=https://curl.se/download/curl-$VERSION.tar.xz
SHA256=8dadd7d2536a314f982ad05a651b713f426bf7d02cc2c76cffb9796f2218e6c5
DEPENDS="mbedtls zlib ca-certificates"

build()
{
    aquila_config_sub

    ./configure --host=$TARGET --prefix=/usr --disable-shared --enable-static \
        --with-mbedtls="$STAGING/usr" --with-zlib="$STAGING/usr" \
        --with-ca-bundle=/etc/ssl/certs/ca-certificates.crt --without-ca-path \
        --disable-threaded-resolver --disable-ldap --disable-ldaps \
        --without-libpsl --without-nghttp2 --without-libidn2 --without-brotli \
        --without-zstd --without-libssh2 --disable-docs --disable-manual
    make -j"$JOBS"
    install -D -m 755 src/curl "$DESTDIR/usr/bin/curl"
    $STRIP "$DESTDIR/usr/bin/curl"
}
