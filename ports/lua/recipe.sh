# Lua, https://www.lua.org

NAME=lua
VERSION=5.5.1
URL=https://www.lua.org/ftp/lua-$VERSION.tar.gz
SHA256=1c4b4068d67061f2a2231ad2b5422e77acea1487ea9890f6320af614f4373dce

build()
{
    # newlib has no _setjmp/_longjmp, its setjmp/longjmp don't save the
    # signal mask either, so they are equivalent
    make -C src -j"$JOBS" posix CC="$CC" AR="$AR rc" RANLIB="$RANLIB" \
        MYCFLAGS="-Os -D_setjmp=setjmp -D_longjmp=longjmp"

    install -d "$DESTDIR/usr/bin"
    install -m 755 src/lua src/luac "$DESTDIR/usr/bin/"
}
