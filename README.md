# Aquila OS ![https://travis-ci.org/mohamed-anwar/Aquila](https://api.travis-ci.org/mohamed-anwar/Aquila.svg?branch=master)
UNIX-like Operating System, including the kernel and system tools.
Intended to be fully **POSIX** compliant. Oh, and it compiles with -O3.

#### CPU-based Features:
##### Supported Archetictures:
> - [X] x86
> - [ ] IA64

- [X] Multitasking
- [ ] SMP

#### Kernel Features:
- [X] Monolitihic kernel
- [X] Virtual Filesystem
- [ ] Supports loadable modules

#### Supported Filesystems:
- [X] initramfs (CPIO Archive filesystem, used for Ramdisk, read only)
- [X] ext2 (Basic Extended 2 filesystem, no caching, read/write)
- [X] devfs (Virtual filesystem, used for device handlers, read/write (device dependent))
- [X] devpts (Virtual filesystem, used for psudo-terminals)

#### Supported Devices:
- [X] ramdev  (Memory mapped device, generic handler)
- [X] i8042   (PS/2 Controller)
- [X] ps2kbd  (PS/2 Keyboard Controller)
- [X] console (IBM TGA console)
- [X] ata     (ATA Harddisk handler, PIO mode)
- [X] fbdev   (Generic framebuffer device handler)

#### Supported video interfaces:
- [X] VESA 3.0

#### System Feautres:
- [ ] Uses Newlib C
