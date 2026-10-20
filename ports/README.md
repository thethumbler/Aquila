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

`build()` runs in the extracted source directory, after applying any
`<name>/*.patch` (`patch -p1`), with `CC`, `AR`, `RANLIB` and
`STRIP` set for the `i686-aquila` toolchain and `JOBS` for parallel
builds. The archive must extract to `<name>-<version>/`.

A port without a usable release archive comes from git: `GIT` (the
repository) and `COMMIT` (a full commit hash, which pins the contents)
replace `URL` and `SHA256`, and the sources are exported as
`<name>-<version>/`.

Other optional settings: `DEPENDS` (recipes to build first, ports or
contrib/ ones), `FILE` (name
of the downloaded archive). Libraries and headers other ports build
against go into `$STAGING`, which `CPPFLAGS` and `LDFLAGS` point at.

## Ports

| port  | version  |                                |
|-------|----------|--------------------------------|
| kilo  | 323d93b  | `kilo` in `/usr/bin`           |
| lua   | 5.5.1    | `lua` and `luac` in `/usr/bin` |
