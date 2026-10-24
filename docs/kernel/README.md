# The AquilaOS kernel

These documents explain how the AquilaOS kernel works: what each part of
it does, how, and why it is done that way. They are written for people
who want to understand operating systems by reading one, and especially
for those writing their own.

AquilaOS is meant to be *boring*. It has no new ideas: it does what UNIX
kernels have done for decades, the plain way, in code small enough to
read in full. So the documents describe the textbook design of each
subsystem, then how AquilaOS implements it, with pointers into the code
and to the specifications it follows. Where AquilaOS takes a shortcut,
they say so.

There are two kinds of documentation:

- **These documents**, one per subsystem, to read in order or on their
  own. Each says what problem the subsystem solves, how it's solved in
  general, how AquilaOS solves it, where the code is, and where to read
  more.
- **The code reference**, generated from the comments in the sources: what
  each function, structure and file is for. `make docs` builds it, with
  these documents, into `build/docs/kernel/api/` (it needs Doxygen);
  `make docs-serve` serves them on <http://localhost:8000/>.

## The kernel at a glance

AquilaOS's kernel is a monolithic UNIX-like kernel. It runs on x86 PCs,
32-bit (i386) and 64-bit (x86_64), one CPU or several, and on aarch64
(QEMU's virt machine, one CPU). These documents describe the x86 PC as
the machine; [The kernel and the machine](architecture.md) says what
depends on it. On the PC it boots from a
Multiboot boot loader (Limine), with an initial RAM disk (initramfs) as
its first root, or with its root on an ext2 disk. User programs are ELF
binaries built against newlib.

### Three layers

The kernel is written so that what is *kernel design* and what is
*the machine* stay apart:

| Layer                | What                                                         | Where                                  |
|----------------------|--------------------------------------------------------------|----------------------------------------|
| **the kernel**       | processes, memory, files, devices, networking: the design, the same on any machine | `kernel/core`, `mm`, `sys`, `fs`, `dev`, `net` |
| **the architecture** | the CPU: its modes, page tables, traps, context switch (x86) | `kernel/arch/i386/` (both i386 and x86_64) |
| **the platform**     | the machine around the CPU: boot loader, interrupt controllers, timers, firmware tables (the PC) | `kernel/arch/i386/platform/` |

The kernel calls the architecture through the `arch_*` and `pmap_*`
functions, and the architecture calls back into the kernel when the
machine does something. [The kernel and the machine](architecture.md)
explains where the line is.

The documents follow the same split. Each one describes the subsystem's
design first, the same on every machine. Sections about one architecture
or platform say so in their title: **"x86: ..."** for the CPU, **"PC:
..."** for the machine around it. If you're here to learn how a kernel
works, the untitled sections are the ones that carry over to any kernel;
the x86 and PC ones are how one machine fills them in.

### The sources

The sources are in `kernel/`:

| Directory      | Layer        | What                                                                |
|----------------|--------------|---------------------------------------------------------------------|
| `core/`        | kernel       | the kernel's entry (`kmain`), its command line, `printk`, the clock |
| `mm/`          | kernel       | memory: the physical page allocator (buddy), the kernel's allocator (vmem, slabs, `kmalloc`), address spaces, page faults |
| `sys/`         | kernel       | processes and threads, the scheduler, `fork` and `exec`, signals, the kernel lock, system calls |
| `fs/`          | kernel       | the virtual filesystem (vnodes, path lookup, files) and the filesystems: initramfs, tmpfs, devfs, devpts, procfs, ext2, minix, pipes |
| `dev/`         | kernel       | devices: the device layer (`kdev`), terminals (virtual terminals, serial ports, pseudo-terminals), ATA disks, the framebuffer, the keyboard and mouse, PCI, the e1000 network card |
| `net/`         | kernel       | sockets, and the network stack: Ethernet, ARP, IPv4, ICMP, UDP, TCP; Unix sockets |
| `include/`     | kernel       | the kernel's headers, and the interface to the architecture (`core/arch.h`, `mm/pmap.h`) |
| `arch/i386/`   | architecture | x86: booting (`boot/`), the CPU (`cpu/`: GDT, IDT, traps, the FPU, SMP), paging (`mm/`), threads, system calls and signal frames (`sys/`), its headers (`include/`) |
| `arch/i386/platform/` | platform | the PC: interrupt controllers (8259, APIC), timers (PIT, HPET), ACPI, CMOS, PS/2 (`misc/`), and their setup (`pc/`) |

The system call interface, shared with the C library, is in the
repository's top-level `include/uapi/`.

## The documents

| Document                                   | About                                                     |
|--------------------------------------------|-----------------------------------------------------------|
| [The kernel and the machine](architecture.md) | the three layers, the `arch_*` interface, where the line is |
| [Booting](booting.md)                      | what the kernel is given, the order of its setup, the first process; Multiboot, paging and long mode on |
| [Interrupts](interrupts.md)                | system calls, faults, device interrupts and the tick, to the kernel; the IDT, the 8259, the APIC, MSI |
| [Memory](memory.md)                        | physical pages, the kernel's allocator, address spaces, page faults; x86's page tables |
| [Processes and threads](processes.md)      | processes, threads, the scheduler, sleeping and waking, `fork`, `exec`, `exit` |
| [SMP and the kernel lock](smp.md)          | several CPUs, the kernel lock, TLB shootdown; starting the other CPUs |
| [System calls](syscalls.md)                | the table, arguments and results, user memory, restarts   |
| [Signals](signals.md)                      | sending, blocking, delivering, handlers, `sigreturn`      |
| [The virtual filesystem](vfs.md)           | vnodes, path lookup, mounts, open files, the caches       |
| [Filesystems](filesystems.md)              | initramfs, tmpfs, devfs, procfs, ext2, minix, pipes       |
| [Devices and terminals](devices.md)        | the device layer, disks, terminals and the line discipline, the console, PCI |
| [Networking](networking.md)                | sockets, the network interfaces, Ethernet, ARP, IPv4, UDP, TCP, Unix sockets |
| [Building and testing](building.md)        | the build, the configurations, the images, the tests      |

## Reading the code

A few conventions help:

- **Comments say what and why**, not how: what a function does and
  returns, what a structure holds, why something is done the way it is. A
  function's comment is right above it; the code reference is built from
  those.
- **Errors are negative `errno` values**: a function returns 0 or a
  positive value on success, `-EINVAL`, `-ENOMEM`, ... on failure. System
  calls hand them back to user space as they are.
- **The architecture's side** of a generic interface is called `arch_*`
  (`arch_thread_switch`, `arch_syscall_return`) and lives in
  `kernel/arch/i386/`; x86-specific functions are called `x86_*`.
- **One kernel lock**: the kernel runs with interrupts off, on one CPU at
  a time (see [Interrupts](interrupts.md) and
  [SMP and the kernel lock](smp.md)); a structure shared by several CPUs
  needs no lock of its own.
