# The CA certificates Mozilla trusts, as curl extracts them, https://curl.se/docs/caextract.html
# (/etc/ssl/certs/ca-certificates.crt, which curl verifies servers with)

NAME=ca-certificates
VERSION=2026-10-20
URL=https://curl.se/ca/cacert-$VERSION.pem
SHA256=796f1389bfea13c0ed78f663b765baaff54a508e5483786bd6ece73c5e3e3cbe
PLAIN=1

build()
{
    install -D -m 644 cacert-$VERSION.pem "$DESTDIR/etc/ssl/certs/ca-certificates.crt"
    ln -sf certs/ca-certificates.crt "$DESTDIR/etc/ssl/cert.pem"
}
