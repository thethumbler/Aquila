# Booting

When a PC is switched on, its firmware (the BIOS, or UEFI) finds a boot
loader on a disk and runs it. The boot loader loads the kernel into
memory, and whatever else the kernel asks for, and jumps to it. From
there the kernel is on its own: it has to set up the CPU, find out how
much memory there is and take it over, find its devices, mount a root
filesystem, and start the first program, *init*, in user mode. Every
other process descends from init.

This document follows AquilaOS from the boot loader's jump to init's
first instruction: how the kernel is loaded, how it turns paging on
(and, on x86_64, long mode), the order in which the kernel's parts are
set up, and how the first process is made.

## The boot loader

### Multiboot

AquilaOS is loaded with the *Multiboot* protocol (version 1), which GRUB,
Limine and QEMU's `-kernel` all speak. A Multiboot kernel is an ELF file
with a *Multiboot header* in its first 8 KiB: a magic number, flags that
say what the kernel wants, and a checksum. AquilaOS asks for:

- its sections page-aligned (`MULTIBOOT_PAGE_ALIGN`);
- the memory map (`MULTIBOOT_MEMORY_INFO`);
- a graphics mode, 1280×800 with 32 bits a pixel, which the boot loader
  may change (`MULTIBOOT_VIDEO_MODE`);
- on x86_64 only, to be loaded by the addresses in the header
  (`MULTIBOOT_AOUT_KLUDGE`): the boot loaders only load 32-bit ELF files,
  so the 64-bit kernel says where its file goes instead, and its linker
  script lays the file out as it is in memory.

The boot loader loads the kernel at 1 MiB (its physical address), loads
the *modules* (the initial RAM disk), and jumps to the kernel's entry,
`_start`, in 32-bit protected mode with paging off, interrupts off, and
flat segments. `%eax` holds Multiboot's magic number, `%ebx` the physical
address of the *Multiboot information*: the kernel's command line, the
memory map, the modules, the framebuffer, and the kernel's ELF section
headers (its symbols, for backtraces).

### Limine

The disk images boot with [Limine](https://github.com/limine-bootloader/limine),
which loads the kernel with its `multiboot1` protocol. Its configuration
is in `boot/`:

| File                    | Used by                                                          |
|-------------------------|------------------------------------------------------------------|
| `boot/limine.conf`      | the CD image (`make iso`): the kernel and `initrd.img`           |
| `boot/limine-hdd.conf`  | the hard disk (`make hdd`): the root filesystem is `root=/dev/hda2`, no module |
| `boot/limine-live.conf` | the live disk (`make live`): the kernel and initrd, gzipped     |

Each has an entry with the console on the serial port (`earlycon=ttyS0`).
`make run` doesn't need a disk: QEMU loads the kernel and the initrd
itself (`-kernel`, `-initrd`).

## Where the kernel is

The kernel is linked to run in the *higher half* of the address space,
where it is the same in every process's address space and out of user
space's way, but it is loaded at 1 MiB, before paging is on. The linker
scripts make the difference (`kernel/arch/i386/kernel.i386.ld`,
`kernel.x86_64.ld`):

- The boot code (`kernel/arch/i386/boot/`) is linked at its physical
  address, from 1 MiB: it is what runs before paging is on.
- Everything else is linked at `_VMA` plus its physical address: `_VMA`
  is `0xC0000000` on i386 (the kernel at 3 GiB + 1 MiB), and
  `0xFFFFFFFF80000000` on x86_64 (the kernel in the top 2 GiB, where code
  can use 32-bit addresses: the *kernel code model*). Each section's load
  address (`AT(...)`) is its physical one.

The `VMA()` and `LMA()` macros (`kernel/arch/i386/include/mm/mm.h`) go from
one to the other.

## Paging on

The entry, `_start` ([`boot/multiboot.S`](../../kernel/arch/i386/boot/multiboot.S)),
saves `%eax` and `%ebx`, loads its own stack (Multiboot gives it none),
and calls the next step, which turns paging on with *boot page tables*
that map the kernel twice: where it is, so that the code running at its
physical address keeps running, and where it is linked.

### i386

`x86_bootstrap()` ([`boot/init.c`](../../kernel/arch/i386/boot/init.c)),
in C, at its physical address, maps the first 768 MiB of memory (the
whole *direct map*, see below) at 0 and at `0xC0000000` with 4 KiB pages,
from a page directory and tables in the boot code's own `.bss`. It turns
paging on, and write protection in the kernel too (`CR0.WP`: copy on
write depends on it), moves the stack pointer to its higher-half address
(`early_init_fix_stack`, in [`boot/sys.S`](../../kernel/arch/i386/boot/sys.S)),
and calls `x86_cpu_init()`, now at its kernel address.

### x86_64

On x86_64 the CPU must also be brought from 32-bit protected mode to
*long mode*, which needs paging. `x86_64_bootstrap`
([`boot/x86_64_bootstrap.S`](../../kernel/arch/i386/boot/x86_64_bootstrap.S)),
32-bit code, builds four-level page tables in the kernel's `.bss`: a page
directory of 2 MiB pages mapping the first GiB, reached three times:

| Where                          | Why                                                |
|--------------------------------|----------------------------------------------------|
| 0                              | the code that turns paging on runs there           |
| `0xFFFF800000000000`           | the direct map                                     |
| `0xFFFFFFFF80000000` (−2 GiB)  | the kernel                                         |

Then, as the Intel manual says: PAE on (`CR4.PAE`, and global pages),
`CR3` loaded, long mode enabled in the `EFER` MSR, paging and write
protection on in `CR0`. The CPU is then in long mode's *compatibility
mode*; a far jump through a small GDT with a 64-bit code segment brings it
to 64-bit mode. A last jump goes to `x86_64_start`, at the kernel's
address, which loads a 64-bit stack and calls `x86_cpu_init()`.

From there both kernels run the same C code.

## Setting up the kernel

`x86_cpu_init()` ([`cpu/init.c`](../../kernel/arch/i386/cpu/init.c))
sets up the kernel in order; each step needs the ones before it.

1. **The boot CPU's `struct cpu`** (`cpus[0]`), and its GDT and TSS
   (`x86_gdt_setup()`). The GDT has the kernel's and user mode's segments;
   `%gs` is made to point at the CPU's `struct cpu`, so `curcpu()` and
   `curthread` work from here on (see [Interrupts](interrupts.md)).
2. **The kernel lock** is taken: the boot CPU holds it until init runs.
3. **The early console** (`earlycon_init()`): `printk()` writes to the
   first serial port from now on.
4. **The IDT** and the exception handlers (`x86_idt_setup()`,
   `x86_isr_setup()`), so that a fault from here on is reported, not a
   triple fault and a reset; the FPU (`x86_fpu_setup()`).
5. **The Multiboot information** is turned into a `struct boot`
   (`process_multiboot_info()`, [`include/boot/boot.h`](../../kernel/arch/i386/include/boot/boot.h)):
   the command line, the memory map, the modules, the symbol table. The
   framebuffer, if the boot loader set a graphics mode, is added to the
   memory map as reserved, and registered for the early console and for
   `/dev/fb0`. `struct boot` (`kernel/include/boot/boot.h`) is what the
   rest of the kernel knows of the boot loader; `__kboot` points to it.
6. **Memory** (`mm_setup()`, `kvmem_setup()`): see below.
7. **The command line** is parsed (`kargs_parse()`,
   [`core/kargs.c`](../../kernel/core/kargs.c)) into `key` and
   `key=value` arguments, which any part of the kernel looks up with
   `kargs_get()`. Only now can the early console move to the screen
   (`earlycon_reinit()`): to the framebuffer if there is one, unless
   `earlycon=` says `ttyS0`, `vga` or `fb`.
8. **The platform** (`platform_init()`,
   [`platform/pc/init.c`](../../kernel/arch/i386/platform/pc/init.c)):
   PCI's configuration ports, the interrupt controllers (the APIC, or the
   8259s, see [Interrupts](interrupts.md)), the PS/2 controller, the PIT,
   the CMOS clock, and the other CPUs (below).
9. **`kmain()`**, the architecture-independent rest.

### Memory

The memory map says which ranges of physical memory are RAM (*usable*)
and which aren't: the BIOS's, the devices', ACPI's. `mm_setup()`
([`mm/mm.c`](../../kernel/mm/mm.c)):

1. Finds the end of RAM, and how much there is. On i386 only the first
   768 MiB are used: what fits in the direct map.
2. Sets aside room for its own structures, before there is an allocator
   to ask: `pages[]`, a `struct vm_page` for each physical page, and the
   buddy allocator's bitmaps. They go in the first free RAM after the
   kernel that the boot page tables map (`boot_mem_place()`), and are
   handed out from there by `mm_boot_alloc()`.
3. Sets up the *buddy allocator*, the physical page allocator, over all of
   RAM, and marks used what isn't free: the kernel, those structures, the
   memory map's holes and reserved ranges, the modules, and the SMP
   trampoline's page.
4. Builds the kernel's page tables (`pmap_init()`): the boot tables become
   the kernel's. On x86_64 the direct map is extended to all of RAM (with
   2 MiB pages); on both, the kernel's half gets all its top-level entries
   now, so that every address space made later shares them; and the
   mapping at 0 is removed, since user space goes there.

`kvmem_setup()` then starts the kernel's own allocator, `kmalloc()`. The
memory document (planned) will cover them all.

### The other CPUs

With the APIC, `platform_init()` starts the other CPUs, the *application
processors*, with `x86_smp_init()` ([`cpu/smp.c`](../../kernel/arch/i386/cpu/smp.c)),
unless `smp=off`. Their local APICs are listed in ACPI's MADT. Each one
is sent an INIT IPI and a startup IPI; it starts in 16-bit real mode at
the *trampoline*, a page below 1 MiB (`0x8000`, `cpu/trampoline.S`),
which brings it through protected mode (and long mode) to the kernel's
`x86_ap_start()`, on its own stack and with its own GDT, TSS and
`struct cpu`. It says it's there and waits, idle, until the scheduler
starts.

## kmain

`kmain()` ([`core/main.c`](../../kernel/core/main.c)) is the same on every
architecture:

1. With `kvmem.test` or `kvmem.guard` on the command line, it checks the
   kernel's allocator (the `kvmem` test).
2. **The devices and filesystems**: the device layer (`kdev_init()`), the
   VFS (`vfs_init()`), then every *built-in module* (`modules_init()`,
   [`core/module.c`](../../kernel/core/module.c)). A driver or filesystem
   declares its init function with `MODULE_INIT()`, which puts a pointer
   to it in the `.__minit` section; the linker script gathers them between
   `__minit` and `__minit_end`, and `modules_init()` calls them all. The
   terminals, the serial ports, the disks, the network card, the
   filesystems: all register themselves this way.
3. **The root filesystem**:
   - with `root=` on the command line (`root=/dev/hda2`), that disk
     partition is mounted, as ext2 unless `rootfstype=` says otherwise;
   - otherwise the first module is the *initramfs*, a `cpio` archive
     (`make initrd`), mounted as it is (`load_ramdisk()`,
     [`fs/initramfs/initramfs.c`](../../kernel/fs/initramfs/initramfs.c)):
     a read-only filesystem whose files are read from the module's memory,
     where the boot loader left it;
   - with neither, the kernel panics.
4. **`/dev`**: devfs is mounted on it, with `/dev/console`, which init may
   write to at once.
5. **init**, the first process: below.

## The first process

There is no process to `fork()` the first one from, so it's made by hand.
`kmain()` makes an empty process (`proc_new()`), makes it the current one,
switches to its address space, and loads `/init` into it
(`binfmt_load()`): an ELF program, whose segments are mapped as for
`exec`. In the initramfs, `/init` is a link to runit's `/sbin/runit-init`.
`arch_proc_init()` gives its thread a kernel stack and its user-mode
registers: the program's entry point, the top of the user stack.
`arch_init_execve()` puts the arguments and the environment on the user
stack, as `execve()` does; init has one argument, the module's command
line, and no environment.

`sched_init_spawn()` ([`sys/sched.c`](../../kernel/sys/sched.c)) then
starts the system:

1. init's working directory is the root, its umask `022`, and it leads a
   new session.
2. `arch_sched_init()` starts the clock (the TSC or the HPET) and the
   scheduler's tick (see [Interrupts](interrupts.md)), and wakes the other
   CPUs, which start their own ticks.
3. init's thread is spawned (`arch_thread_spawn()`,
   [`sys/thread.c`](../../kernel/arch/i386/sys/thread.c)): the CPU moves to
   its kernel stack, the kernel lock is let go, and `x86_jump_user()`
   `iret`s into user mode, at init's first instruction.

The boot stack is never used again. From here on, the kernel only runs
when something enters it: a system call, an interrupt, an exception.

## A boot

What `make run` shows (x86_64, two CPUs, shortened):

```
x86: Welcome to AquilaOS!
x86: installing IDT
x86: installing ISRs
x86: processing multiboot info block at 0x0000000000009500
kernel: 255 MiB of memory, up to 0x000000000FC00000
buddy: Setting up buddy allocator (total memory 0x000000000FC00000)
buddy: set unusable: 0x0000000000100000-0x0000000000254FFF
...
x86_64: 256 MiB in the direct map
kernel: cmdline: build/x86_64/kernel/kernel-0.0.1.x86_64 earlycon=ttyS0
x86: Initializing PCI
apic: local APIC 0 at 0x00000000FEE00000, 1 I/O APIC(s), the first at 0x00000000FEC00000 (GSI 0, 24 pins)
x86: interrupts through the apic
i8254: Initializing [0x0000000000000040 (pio)]
cmos: initializing [0x0000000000000070 (pio)]
smp: 2 CPUs
kdev: initializing
kernel: loading builtin modules
...
e1000: 8086:100E at 0x00000000FEBC0000, irq 11, link up
kernel: loading ramdisk
kernel: loading init process
clock: hpet, 10000000 fs a count
apic: the timer every 2000000 ns (125028 counts, divided by 16)
AquilaOS version v0.0.2
login:
```

The first `buddy: set unusable` is the kernel itself, from 1 MiB. The
command line QEMU passes starts with the kernel's file name, which the
kernel ignores.

## The command line

The arguments the kernel reads at boot:

| Argument                 | What                                                        |
|--------------------------|-------------------------------------------------------------|
| `earlycon=`              | where the kernel's messages go: `ttyS0`, `vga` or `fb` (default: the framebuffer if there is one, else the serial port) |
| `root=/dev/hdXN`         | the root filesystem is this partition, not the initramfs    |
| `rootfstype=`            | the root filesystem's type: `ext2` (the default) or `minix` |
| `apic=off`               | the 8259s, not the APIC (and one CPU)                       |
| `smp=off`                | the boot CPU only                                           |
| `kvmem.test`, `kvmem.guard` | check the kernel's allocator at boot                     |

## Where it is

| File                                            | What                                         |
|-------------------------------------------------|----------------------------------------------|
| `boot/limine*.conf`                             | the boot loader's configurations             |
| `kernel/arch/i386/kernel.i386.ld`, `kernel.x86_64.ld` | where the kernel is linked and loaded |
| `kernel/arch/i386/boot/multiboot.S`             | the Multiboot header, the entry              |
| `kernel/arch/i386/boot/init.c`, `sys.S`         | i386: paging on                              |
| `kernel/arch/i386/boot/x86_64_bootstrap.S`      | x86_64: paging and long mode on              |
| `kernel/arch/i386/include/boot/boot.h`          | the Multiboot information to `struct boot`   |
| `kernel/arch/i386/cpu/init.c`                   | `x86_cpu_init()`: the order of it all        |
| `kernel/arch/i386/cpu/gdt.c`                    | the GDT and the TSS                          |
| `kernel/arch/i386/earlycon/`                    | the early console: serial, VGA, framebuffer  |
| `kernel/mm/mm.c`                                | `mm_setup()`: the memory map, the boot memory |
| `kernel/arch/i386/mm/i386.c`, `x86_64.c`        | `pmap_init()`: the kernel's page tables      |
| `kernel/arch/i386/platform/pc/init.c`           | `platform_init()`: the PC's devices          |
| `kernel/arch/i386/cpu/smp.c`, `trampoline.S`    | the other CPUs                               |
| `kernel/core/kargs.c`                           | the command line                             |
| `kernel/core/module.c`                          | the built-in modules                         |
| `kernel/core/main.c`                            | `kmain()`: the root filesystem, init         |
| `kernel/sys/sched.c`                            | `sched_init_spawn()`                         |

## References

- *The Multiboot Specification*, version 0.6.96, GNU: the header, the
  machine state at the entry, the information structure and the memory
  map.
- Intel® 64 and IA-32 Architectures Software Developer's Manual, Volume 3:
  *Paging* (32-bit and 4-level paging), *Mode Switching* (9.8, initializing
  IA-32e mode) and *Multiple-Processor Initialization* (9.4, INIT and
  startup IPIs).
- *System V Application Binary Interface, AMD64 Architecture Processor
  Supplement*: the kernel code model (3.5.1).
- Limine's [`CONFIG.md`](https://github.com/limine-bootloader/limine/blob/trunk/CONFIG.md):
  its configuration file.
- The OSDev wiki's pages on [Multiboot](https://wiki.osdev.org/Multiboot),
  [Higher Half Kernel](https://wiki.osdev.org/Higher_Half_Kernel) and
  [Setting Up Long Mode](https://wiki.osdev.org/Setting_Up_Long_Mode).
