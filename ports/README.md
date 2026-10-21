# Ports

Optional third-party software, built for AquilaOS from upstream sources.
Third-party software that is part of the base system is in
[contrib/](../contrib/) instead; both are recipes built by
[mk/recipes.sh](../mk/recipes.sh).

```
make ports                  # build every port
mk/recipes.sh ports lua     # build some ports
make initrd                 # the initrd includes the ports once built
```

Ports are installed into `build/ports/root`, which `make initrd` copies
into the root filesystem after the base system.

## Recipes

A port is `ports/<name>/recipe.sh` (a contrib/ recipe is the same):

```sh
NAME=foo
VERSION=1.0
URL=https://example.org/foo-$VERSION.tar.gz
SHA256=<sha256 of the archive>

build()
{
    make CC="$CC" AR="$AR" RANLIB="$RANLIB"
    install -D -m 755 foo "$DESTDIR/usr/bin/foo"
}
```

Recipes may call `aquila_config_sub` in `build()` to let an autoconf
`configure` accept `--host=i686-aquila`. A dependency is rebuilt only
when its recipe or libc changed since it was last built.

`build()` runs in the extracted source directory, after applying any
`<name>/*.patch` (`patch -p1`), with `CC`, `AR`, `RANLIB` and
`STRIP` set for the `i686-aquila` toolchain and `JOBS` for parallel
builds. The archive must extract to `<name>-<version>/`, or to the
directory `ARCHIVE_DIR` names.

A port without a usable release archive comes from git: `GIT` (the
repository) and `COMMIT` (a full commit hash, which pins the contents)
replace `URL` and `SHA256`, and the sources are exported as
`<name>-<version>/`.

Other optional settings: `DEPENDS` (recipes to build first, ports or
contrib/ ones), `FILE` (name
of the downloaded archive). Libraries and headers other ports build
against go into `$STAGING`, which `CPPFLAGS` and `LDFLAGS` point at,
and `pkg-config` only looks there (`--static`: libraries are static).
Libtool archives (`.la`) are removed from it, they name the build
host's `/usr/lib`.

## Ports

| port       | version  |                                                     |
|------------|----------|-----------------------------------------------------|
| font-cursor-misc | 1.0.4 | the cursor font, for X servers                 |
| font-misc-misc | 1.1.3  | the fixed width fonts of X (`fixed`, `6x13`, `9x15`, ...) in `/usr/share/fonts/X11/misc` (BDF, there is no `bdftopcf`): Unicode, and ISO 8859-1 (made by the recipe) which the usual names are |
| kilo       | 323d93b  | `kilo` in `/usr/bin`                                |
| libfontenc | 1.1.8    | library only (for libXfont)                         |
| libice     | 1.1.2    | library only (for libSM)                            |
| libpthread-stubs | 0.5 | `pthread-stubs.pc` only (the pthread functions are in the C library), for libxcb |
| libsm      | 1.2.6    | library only (session management, for Xt)           |
| libtermkey | 0.22     | library only (for vis)                              |
| libx11     | 1.8.12   | library only (Xlib), and its data in `/usr/share/X11` (locales, error messages) |
| libxau     | 1.0.12   | library only (X authorization)                      |
| libxaw     | 1.0.16   | library only (Athena widgets)                       |
| libxcb     | 1.17.0   | library only (the X protocol, under Xlib)           |
| libxdmcp   | 1.1.5    | library only (for TinyX)                            |
| libxext    | 1.3.6    | library only (X extensions)                         |
| libxfont   | 1.5.4    | library only, bitmap fonts (for TinyX)              |
| libxmu     | 1.2.1    | library only (miscellaneous utilities of X clients) |
| libxpm     | 3.5.17   | library only (X pixmaps)                            |
| libxt      | 1.3.1    | library only (the X Toolkit Intrinsics)             |
| lua        | 5.5.1    | `lua` and `luac` in `/usr/bin`                      |
| ncurses    | 6.6      | library only, wide characters, the vt100, ansi, linux, xterm and xterm-256color descriptions built in (no terminfo database) |
| tcc        | mob e23dabe | `tcc` (also `cc`) in `/usr/bin`, compiling static programs on AquilaOS; with the C library's headers in `/usr/include` and archives in `/usr/lib`; no `-run` (no `mprotect()`) |
| tinyx      | 1.2.0 (feab72c) | `Xfbdev` (also `X`) and `startx` (X and twm with an xterm, or `~/.xinitrc`) in `/usr/bin`: X on the framebuffer, the keyboard and mouse of a virtual terminal; clients through a Unix socket (`:0`). Its changes to X.Org's code are GPLv3 |
| twm        | 1.0.13.1 | `twm` in `/usr/bin`, the window manager of X        |
| vis        | 0.9      | `vis` in `/usr/bin`, without Lua (no syntax highlighting) |
| xcb-proto  | 1.17.0   | for libxcb: the protocol in XML, generated on the build host |
| xorgproto  | 2024.1   | headers only (X protocol)                           |
| xterm      | 401      | `xterm` in `/usr/bin`, the terminal emulator of X (bitmap fonts, no setuid, utmp or luit) |
| xtrans     | 1.5.2    | headers only (X transports)                         |
| zlib       | 1.3.1    | library only                                        |
