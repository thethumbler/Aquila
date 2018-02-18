---
layout: page
title: Features
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
- ramdev  (Memory mapped device, generic handler)
```
 kernel/devices/ramdev.c
```
- i8042   (PS/2 Controller)
```
 kernel/devices/i8042.c
```
- ps2kbd  (PS/2 Keyboard Controller)
```
 kernel/devices/ps2kbd.c
```
- console (IBM TGA console)
```
 kernel/devices/console.c
```
- ata     (ATA Harddisk handler, PIO mode)
```
 kernel/devices/bus/ata.c
```
- fbdev   (Generic framebuffer device handler)
```
 kernel/devices/video/fbdev/
```

#### Supported video interfaces:
- VESA 3.0
```
 kernel/devices/video/fbdev/vesa.c
```


#### System Feautres:
- Uses newlib C Library
