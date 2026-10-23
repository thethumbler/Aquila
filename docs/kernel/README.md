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

AquilaOS's kernel is a monolithic UNIX-like kernel for x86: 32-bit (i386)
and 64-bit (x86_64) PCs, one CPU or several. It boots from a Multiboot
boot loader (Limine), with an initial RAM disk (initramfs) as its first
root, or with its root on an ext2 disk. User programs are ELF binaries
built against newlib.

The sources are in `kernel/`:

| Directory      | What                                                                |
|----------------|---------------------------------------------------------------------|
| `arch/i386/`   | the x86 code, both i386 and x86_64: booting, the CPU (GDT, IDT, interrupts, the FPU, SMP), paging, the PC's devices (APIC, 8259, PIT, ACPI, HPET), context switching, system call entry, signal frames |
| `core/`        | the kernel's entry (`kmain`), its command line, `printk`, the clock |
| `mm/`          | memory: the physical page allocator (buddy), the kernel's allocator (vmem, slabs, `kmalloc`), address spaces, page faults |
| `sys/`         | processes and threads, the scheduler, `fork` and `exec`, signals, the kernel lock, system calls |
| `fs/`          | the virtual filesystem (vnodes, path lookup, files) and the filesystems: initramfs, tmpfs, devfs, devpts, procfs, ext2, minix, pipes |
| `dev/`         | devices: the device layer (`kdev`), terminals (virtual terminals, serial ports, pseudo-terminals), ATA disks, the framebuffer, the keyboard and mouse, PCI, the e1000 network card |
| `net/`         | sockets, and the network stack: Ethernet, ARP, IPv4, ICMP, UDP, TCP; Unix sockets |
| `include/`     | the headers shared by all of it                                     |

The system call interface, shared with the C library, is in the
repository's top-level `include/uapi/`.

## The documents

| Document                         | About                                                     |
|----------------------------------|-----------------------------------------------------------|
| [Booting](booting.md)            | the boot loader, paging and long mode on, the order of the kernel's setup, the first process |
| [Interrupts](interrupts.md)      | the IDT, the entry path, the 8259, the APIC, MSI, dispatch, the timer, IPIs, exceptions |

Planned:

- Memory: physical pages, the kernel's allocator, address spaces, page faults
- Processes, threads and the scheduler
- SMP and the kernel lock
- System calls and user memory
- Signals
- The virtual filesystem: vnodes, path lookup, files
- Devices and terminals
- Filesystems: initramfs, tmpfs, procfs, ext2, minix
- Networking: sockets, IPv4, TCP
- Building and testing the kernel

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
  a time (see [Interrupts](interrupts.md)); a structure shared by several
  CPUs needs no lock of its own.
