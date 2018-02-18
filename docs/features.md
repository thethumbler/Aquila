---
layout: page
title: About Aquila
subtitle: You know it rocks already
---

# Aquila OS
UNIX-like Operating System, including the kernel and system tools.
Intended to be fully **POSIX** compatible.

#### CPU-based Features:
##### Supported Archetictures:
- x86

#### Kernel Features:
- Monolitihic kernel
- Virtual Filesystem

#### Supported Filesystems:
- initramfs (CPIO Archive filesystem, used for Ramdisk, read only)
```
kernel/fs/initramfs/
```
- ext2 (Basic Extended 2 filesystem, no caching, read/write)
```
kernel/fs/ext2/
```
- devfs (Virtual filesystem, used for device handlers, read/write (device dependent))
```
kernel/fs/devfs/
```
- devpts (Virtual filesystem, used for psudo-terminals)
```
kernel/fs/devpts/
```

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
- [ ] Has _in house_ C library
