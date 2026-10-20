# Aquila OS [![build](https://github.com/thethumbler/aquila/actions/workflows/build.yml/badge.svg)](https://github.com/thethumbler/aquila/actions/workflows/build.yml)

UNIX-like operating system, including the kernel, C library and system
tools. Intended to be fully **POSIX** compliant.

## Building

Everything is built from source into `build/`. On a Linux host you need
a C/C++ compiler with 32-bit support, `make`, `bison`, `flex`, `texinfo`,
GMP, MPFR and MPC development headers, `wget`, `nasm`, `cpio`, `xorriso`
and `qemu-system-i386`. On Debian/Ubuntu:

```
sudo apt install build-essential gcc-multilib bison flex texinfo libgmp-dev \
    libmpfr-dev libmpc-dev wget nasm cpio xorriso qemu-system-x86
```

Then:

```
make toolchain      # i686-aquila gcc/binutils, newlib and Limine (once, ~15 min)
make                # kernel, user space programs, contrib/ and initrd
make ports          # optional software (vis, kilo, Lua, ...), included in the initrd
make run-iso        # build the ISO and boot it in QEMU
```

As on the BSDs, some third-party software is part of the base system:
[contrib/](contrib/) (dash as `/bin/sh`, sbase for the core utilities,
runit as init) is fetched from upstream and built by `make`. [ports/](ports/) is
optional software, built with `make ports`.

`make run` boots the kernel directly with the console on the serial port
(log in as `root`, no password). On the screen (`make run-iso`), the
kernel's virtual terminals have logins on `/dev/tty1` to `/dev/tty3`,
Alt+F1 to Alt+F6 switch between them. runit supervises the logins as
services (`/etc/sv`, enabled in `/etc/service`, see `sv`); `poweroff` and
`reboot` shut down through it, as does Ctrl+Alt+Del. `make iso` only builds
`build/aquila.iso`.

The ISO and `make run` boot the ramdisk, read-only and the same on every
boot. `make hdd` makes `build/aquila.img`, a hard disk with AquilaOS
installed: Limine on a FAT32 `/boot`, the root filesystem on ext2
(`root=/dev/hda2`), where what is written is kept. `make run-hdd` boots
it (making it the first time) with the system just built: `make
hdd-update` puts the kernel and the system's files on it and keeps the
rest (home directories, and the accounts in `/etc`), `make hdd-kernel`
only the kernel, `make hdd` makes the whole disk anew. See the top of the [Makefile](Makefile)
for all targets.

## Testing

```
make test           # or tests/run.sh
```

boots the kernel in QEMU with test programs and checks the system built
from source, see [tests/](tests/README.md).

## Layout

| directory                    |                                                    |
|------------------------------|----------------------------------------------------|
| [kernel/](kernel/)           | the kernel                                         |
| [include/uapi/](include/uapi/aquila) | user/kernel ABI: syscall numbers, errno, structures shared by the kernel and libc |
| [libc/](libc/aquila)         | AquilaOS port of newlib                            |
| [toolchain/](toolchain/)     | cross toolchain build script and patches           |
| [userland/](userland/)       | aqbox (system tools: login, ps, mount, ...)        |
| [contrib/](contrib/)         | third-party software of the base system (recipes)  |
| [ports/](ports/)             | optional third-party software (recipes)            |
| [mk/](mk/)                   | build script for contrib/ and ports/ recipes       |
| [rootfs/](rootfs/)           | root filesystem skeleton                           |
| [boot/](boot/)               | bootloader configuration                           |
| [configs/](configs/)         | kernel build configurations                        |
| [tests/](tests/)             | QEMU based test suite                              |

## Features

#### Architectures
- [X] x86
- [ ] x86-64

- [X] Multitasking
- [X] Multithreading
- [ ] SMP

#### Kernel
- [X] Monolithic kernel
- [X] Virtual Filesystem
- [ ] Loadable modules

#### Filesystems
- [X] initramfs (CPIO archive, read only)
- [X] tmpfs     (temporary filesystem, read/write)
- [X] devfs     (device files, read/write)
- [X] devpts    (pseudo-terminals, read/write)
- [X] procfs    (process information, read only)
- [X] ext2      (read/write)
- [X] minix     (read/write)
- [ ] ext3
- [ ] ext4

#### Devices
- [X] i8042   (PS/2 controller)
- [X] ps2kbd  (PS/2 keyboard)
- [X] ps2mouse (PS/2 mouse)
- [X] ramdev  (memory mapped devices)
- [X] vt      (virtual terminals on the framebuffer or VGA text mode)
- [X] ata     (ATA hard disks, PIO mode)
- [X] fbdev   (framebuffer, VESA)
- [X] 8250    (UART)

#### System
- [X] newlib 4.6 C library
- [X] dash    (shell, contrib)
- [X] sbase   (core utilities, contrib)
- [X] runit   (init and service supervision, contrib)
- [X] aqbox   (system tools)
- [X] kilo    (text editor, port)
- [X] vis     (vi-like editor, port) on ncurses (port)
- [X] Lua     (port)
- [X] tcc     (C compiler, port), building programs on AquilaOS

## License

AquilaOS is dedicated to the public domain under
[CC0 1.0 Universal](LICENSE): you can copy, modify and distribute it,
even commercially, without asking permission.

Third-party code keeps its own license:

| component | location | license |
|-----------|----------|---------|
| unscii-16 font | [kernel/arch/i386/earlycon/font.h](kernel/arch/i386/earlycon/font.h) | public domain, Viznut |
| multiboot.h | [kernel/arch/i386/include/boot/multiboot.h](kernel/arch/i386/include/boot/multiboot.h) | permissive, Free Software Foundation |

The toolchain patches in [toolchain/patches/](toolchain/patches/) modify
binutils and GCC (GPLv3) and newlib (various free licenses); the patched
tools remain under those licenses. contrib/ and ports/ software is
fetched from upstream and keeps its own license (dash: BSD, sbase: MIT,
kilo: BSD 2-Clause, Lua: MIT, ncurses: MIT-style, libtermkey: MIT,
vis: ISC).
