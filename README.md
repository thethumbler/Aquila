# Aquila OS [![build](https://github.com/thethumbler/aquila/actions/workflows/build.yml/badge.svg)](https://github.com/thethumbler/aquila/actions/workflows/build.yml)

UNIX-like operating system, including the kernel, C library and system
tools. Intended to be fully **POSIX** compliant.

## Building

Everything is built from source into `build/` (the x86-64 system into
`build/x86_64`). On a Linux host you need a C/C++ compiler with 32-bit
support, `make`, `bison`, `flex`, `texinfo`, GMP, MPFR and MPC
development headers, `wget`, `nasm`, `cpio`, `xorriso` and
`qemu-system-x86_64` (and `gperf` for the ports). On Debian/Ubuntu:

```
sudo apt install build-essential gcc-multilib bison flex texinfo libgmp-dev \
    libmpfr-dev libmpc-dev wget nasm cpio xorriso qemu-system-x86 gperf
```

Then:

```
make toolchain      # x86_64-aquila gcc/binutils, newlib and Limine (once, ~15 min)
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
`build/x86_64/aquila.iso`. With the ports (`make ports`), `startx` on the screen
starts X: twm and an xterm (Ctrl+Alt+F1 to Ctrl+Alt+F6 switch terminals
from it).

The ISO and `make run` boot the ramdisk, read-only and the same on every
boot. `make hdd` makes `build/x86_64/aquila.img`, a hard disk with AquilaOS
installed: Limine on a FAT32 `/boot`, the root filesystem on ext2
(`root=/dev/hda2`), where what is written is kept. `make run-hdd` boots
it (making it the first time) with the system just built: `make
hdd-update` puts the kernel and the system's files on it and keeps the
rest (home directories, and the accounts in `/etc`), `make hdd-kernel`
only the kernel, `make hdd` makes the whole disk anew. See the top of the [Makefile](Makefile)
for all targets.

### Network

The kernel drives Intel 8254x Ethernet cards (e1000), QEMU's default;
`lo`, the loopback interface (127.0.0.1), is always there. IPv4 is
configured with the kernel argument `ip=<address>/<prefix
length>[:<gateway>]`, e.g. `ip=10.0.2.15/24:10.0.2.2` for QEMU's user
network, or on the running system:

```
ifconfig eth0 10.0.2.15 netmask 255.255.255.0 up
route add default gw 10.0.2.2
ping 10.0.2.2
```

or from a DHCP server (sdhcp, contrib): `ifconfig eth0 up; sdhcp eth0`,
which sets the address, the default route and `/etc/resolv.conf`, and
renews the lease. On a disk, the `dhcp` service does it at every boot
once enabled: `ln -s /etc/sv/dhcp /etc/service/`.

Programs have TCP (`SOCK_STREAM`), UDP and ping sockets (`SOCK_DGRAM`,
`IPPROTO_ICMP`). Host names are looked up in `/etc/hosts`, then with
the DNS servers `/etc/resolv.conf` names (DHCP's), e.g.
`nc example.com 80`.

### i386

The default is x86-64. AquilaOS also runs on i386 (using at most
768 MiB of memory, what its kernel maps), which is built into
`build/` with an i686-aquila toolchain (next to the x86_64 one in
`build/toolchain`), `CONFIG=i386-pc` selects it:

```
make toolchain CONFIG=i386-pc       # i686-aquila gcc/binutils and newlib
make CONFIG=i386-pc                 # build/
make hdd CONFIG=i386-pc             # build/aquila.img
make test CONFIG=i386-pc            # or tests/run.sh -a i386
```

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
- [X] SMP     (up to 32 CPUs from ACPI's MADT, a kernel lock: user mode
              runs on every CPU at once, the kernel on one at a time;
              `smp=off`)

#### Kernel
- [X] Monolithic kernel
- [X] A clock to the nanosecond: the TSC (invariant) or the HPET, the
      PIT's ticks without them (`clock=tsc|hpet|pit`)
- [X] Interrupts through the APIC (I/O APIC routes from ACPI's MADT, the
      local APIC's timer as the tick, shared IRQs, MSI), the 8259 and the
      PIT without one (`apic=off`); `/proc/interrupts`
- [X] Kernel threads (process 0, `[kernel]`; `ps` shows them as `[net]`, ...)
- [X] POSIX signals, delivered on the way back to user mode (a handler's
      frame keeps the FPU state, `sigreturn` from a kernel-mapped page)
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
- [X] ps2mouse (PS/2 mouse, /dev/mouse as Linux's /dev/psaux)
- [X] ramdev  (memory mapped devices)
- [X] vt      (virtual terminals on the framebuffer or VGA text mode;
              graphics, raw keyboard and switching as the Linux console)
- [X] ata     (ATA hard disks, PIO mode)
- [X] fbdev   (framebuffer, VESA; the Linux framebuffer API)
- [X] 8250    (UART)
- [X] e1000   (Intel 8254x Ethernet, and the 82574L with MSI: QEMU's
              e1000 and e1000e)

#### Network
- [X] IPv4, ARP, ICMP (echo, unreachable), the loopback interface (lo)
- [X] UDP and ping sockets
- [X] TCP     (RFC 9293: retransmission, NewReno congestion control,
              delayed ACKs, Nagle, keepalive; no window scaling, SACK or
              timestamps yet)
- [X] DHCP    (sdhcp, contrib)
- [X] DNS     (the C library's resolver: /etc/hosts, then DNS over UDP,
              TCP for long answers)

#### System
- [X] newlib 4.6 C library
- [X] dash    (shell, contrib)
- [X] sbase   (core utilities, contrib)
- [X] netcat  (OpenBSD's nc, contrib)
- [X] runit   (init and service supervision, contrib)
- [X] aqbox   (system tools)
- [X] kilo    (text editor, port)
- [X] vis     (vi-like editor, port) on ncurses (port)
- [X] Lua     (port)
- [X] tcc     (C compiler, port), building programs on AquilaOS
- [X] TinyX   (Xfbdev, X server on the framebuffer, port) with X.Org's
              fixed and cursor fonts, Xlib and the X Toolkit (ports)
- [X] twm and xterm (ports), `startx`

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
