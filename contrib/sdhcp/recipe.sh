# sdhcp, a small DHCP client, https://git.2f30.org/sdhcp/
# (from git: the 0.1 release predates using the DHCPACK's parameters)

NAME=sdhcp
VERSION=0.1
GIT=https://git.2f30.org/sdhcp/
COMMIT=8455fd2d090bd9b227340c30c92c6aa13302c91a

build()
{
    # newlib has no HOST_NAME_MAX (its nodename is 64 bytes, as uname's)
    $CC -Os -s -std=c99 -D_DEFAULT_SOURCE -DHOST_NAME_MAX=64 -o sdhcp \
        sdhcp.c util/eprintf.c util/strlcpy.c

    install -d "$DESTDIR/sbin"
    install -m 755 sdhcp "$DESTDIR/sbin/sdhcp"
}
