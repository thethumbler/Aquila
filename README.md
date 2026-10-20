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
make                # kernel, user space programs and initrd
make ports          # optional: third-party software (Lua), included in the initrd
make run-iso        # build the ISO and boot it in QEMU
```

`make run` boots the kernel directly with the console on the serial port
(log in as `root`, no password). `make iso` only builds
`build/aquila.iso`. See the top of the [Makefile](Makefile) for all
targets.

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
| [userland/](userland/)       | init, aqbox (core utilities), fbterm, kilo         |
| [ports/](ports/)             | third-party software recipes                       |
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
- [X] console
- [X] ata     (ATA hard disks, PIO mode)
- [X] fbdev   (framebuffer, VESA)
- [X] 8250    (UART)

#### System
- [X] newlib 4.6 C library
- [X] aqbox   (Aquila Box, like busybox)
- [X] fbterm  (framebuffer terminal)
- [X] kilo    (text editor)
- [X] Lua     (port)
- [ ] tcc

## License

AquilaOS is dedicated to the public domain under
[CC0 1.0 Universal](LICENSE): you can copy, modify and distribute it,
even commercially, without asking permission.

Third-party code keeps its own license:

| component | location | license |
|-----------|----------|---------|
| kilo | [userland/kilo/](userland/kilo/LICENSE) | BSD 2-Clause, Salvatore Sanfilippo |
| bim | [userland/aqbox/extra/bim.c](userland/aqbox/extra/bim.c) | ISC, K. Lange |
| libvterm | [userland/fbterm/libvterm/](userland/fbterm/libvterm/LICENSE), `userland/fbterm/include/vterm.h` | MIT, Paul Evans |
| NanoJPEG | [userland/fbterm/nanojpeg.c](userland/fbterm/nanojpeg.c) | MIT, Martin J. Fiedler |
| multiboot.h | [kernel/arch/i386/include/boot/multiboot.h](kernel/arch/i386/include/boot/multiboot.h) | permissive, Free Software Foundation |

The toolchain patches in [toolchain/patches/](toolchain/patches/) modify
binutils and GCC (GPLv3) and newlib (various free licenses); the patched
tools remain under those licenses. Ports are fetched from upstream under
their own licenses.
