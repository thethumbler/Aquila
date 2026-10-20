# Ports

Third-party software built for AquilaOS from upstream sources.

```
make ports              # build every port
ports/build.sh lua      # build some ports
make initrd             # the initrd includes the ports once built
```

Ports are installed into `build/ports/root`, which `make initrd` copies
into the root filesystem.

## Adding a port

Create `ports/<name>/recipe.sh`:

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

`build()` runs in the extracted source directory, after applying any
`ports/<name>/*.patch` (`patch -p1`), with `CC`, `AR`, `RANLIB` and
`STRIP` set for the `i686-aquila` toolchain and `JOBS` for parallel
builds. The archive must extract to `<name>-<version>/`.

A port without a usable release archive comes from git: `GIT` (the
repository) and `COMMIT` (a full commit hash, which pins the contents)
replace `URL` and `SHA256`, and the sources are exported as
`<name>-<version>/`.

Other optional settings: `DEPENDS` (ports to build first), `FILE` (name
of the downloaded archive). Libraries and headers other ports build
against go into `$STAGING`, which `CPPFLAGS` and `LDFLAGS` point at.

## Ports

| port  | version  |                                                        |
|-------|----------|--------------------------------------------------------|
| dash  | 0.5.13.5 | `/bin/dash`, also `/bin/sh`                            |
| kilo  | 323d93b  | `kilo` in `/usr/bin`                                   |
| lua   | 5.5.1    | `lua` and `luac` in `/usr/bin`                         |
| sbase | 0.1      | `/bin/sbase-box`, linked as `ls`, `cp`, `sed`, `grep`, ... in place of aqbox's applets; without `chroot`, `cron`, `flock`, `logger`, `mkfifo`, `nice`, `renice` and `tftp`, which need what the kernel doesn't have yet |
