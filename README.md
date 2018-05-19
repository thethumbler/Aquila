# Aquila OS ![https://travis-ci.org/mohamed-anwar/Aquila](https://api.travis-ci.org/mohamed-anwar/Aquila.svg?branch=master)
![Image of Aquila](http://aquilaos.com/img/screenshot.png)

UNIX-like Operating System, including the kernel and system tools.
Intended to be fully **POSIX** compliant. Oh, and it compiles with -O3.

#### Build Instructions
Follow the instructions in this [wiki page.](https://github.com/mohamed-anwar/Aquila/wiki/Build-Instructions)

#### CPU-based Features:
##### Supported Archetictures:
> - [X] x86
> - [ ] IA64

- [X] Multitasking
- [X] Multithreading
- [ ] SMP

#### Kernel Features:
- [X] Monolitihic kernel
- [X] Virtual Filesystem
- [ ] Supports loadable modules

#### Supported Filesystems:
- [X] initramfs (CPIO Archive filesystem, used for Ramdisk, read only)
- [X] tmpfs     (Generic temporary filesystem, read/write)
- [X] devfs     (Virtual filesystem, used for device handlers, statically populated, read/write)
- [X] devpts    (Virtual filesystem, used for psudo-terminals, dynamically populated, read/write)
- [X] procfs    (Processes information filesystem, read only)
- [X] ext2      (Basic Extended 2 filesystem, read/write)
- [ ] ext3
- [ ] ext4
- [ ] sysfs

#### Supported Devices:
- [X] i8042   (PS/2 Controller)
- [X] ramdev  (Memory mapped device, generic handler)
- [X] ps2kbd  (PS/2 Keyboard Controller)
- [X] console (IBM TGA console)
- [X] ata     (ATA Harddisk handler, PIO mode)
- [X] fbdev   (Generic framebuffer device handler)
- [X] 8250    (UART)

#### Supported video interfaces:
- [X] VESA 3.0

#### System Feautres:
- [X] newlib-3.0.0 (latest)
- [X] aqbox        (Aquila Box, like busybox)
- [X] fbterm       (Framebuffer based terminal)
- [X] tcc          (Tiny C Compiler)
- [X] lua          (Lua programming language)
- [X] kilo         (Kilo text editor)
