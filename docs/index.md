---
layout: page
title: Aquila OS
subtitle: Hobbyist Operating System, just for the heck of it
use-site-title: true
---

### What is it?
Aquila is a complete Operating System (kernel + system) that is designed to be POSIX compliant and mostly ISA transparent.

### What is cool about it anyway?
- All interfaces implemented so far are POSIX compliant.
- It compiles with -O3.
- Lua 5 is ported to it without changing a single line of code.
- It should be simple for anyone to read and understand the code.

### How does it look like?
This is a screenshot of AquilaOS running on qemu with 640x480 resolution. This is `fbterm` (A framebuffer based terminal) running `aqsh` (Aquila Shell - Part of aqbox).

![Image of Aquila](/Aquila/img/screenshot.png)

### How to try it?
Simple, just grab a toolchain, build it, patch newlib and ...

Err...

Maybe you're more interested in just an iso image? Head to [Github releases](https://github.com/mohamed-anwar/Aquila/releases) and grab the latest release iso image there and run it in Qemu with the following command:
- Ubuntu/Debian
`qemu-system-i386 -enable-kvm -cdrom aquila.iso -m 1G`
- Fedora
`qemu-kvm -cdrom aquila.iso -m 1G`

If you're a hardcore programmer (and know-what-you-are-doing) though, you may reference [this post on OSDev.org](https://forum.osdev.org/viewtopic.php?f=2&t=32714) and follow the steps. Or wait for a complete step-by-step guide if you don't know what the heck that is.

### Is that it?
Yes, that is it, that's all there is, maybe you're intereseted in fancy GUI? specific device driver? support for new filesystem? Feel free to contribute to the repo, or wait until I do it.
