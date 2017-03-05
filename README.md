# Aquila OS
UNIX-like Operating System, including the kernel and system tools.
Intended to be fully **POSIX** compatible.

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
- [ ] Compatible with _Linux_ Syscalls (may never be implemented, this is **NOT** a Linux clone)

#### Supported Filesystems:
- [X] cpiofs (CPIO Archive filesystem, used for Ramdisk)
- [ ] Ext2
- [ ] FAT

#### Supported Devices:
- [X] ramdev  (Memory mapped device, generic handler)
- [X] i8042   (PS/2 Controller)
- [X] ps2kbd  (PS/2 Keyboard Controller)


#### System Feautres:
- [ ] Has _in house_ C library
