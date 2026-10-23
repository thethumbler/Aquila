# The kernel and the machine

Most of what a kernel does has nothing to do with the CPU it runs on.
Processes, the scheduler's choices, files, path lookup, pipes, sockets,
TCP: these are the same on a PC, a phone or a mainframe. A smaller part
can't be written without knowing the machine: how to switch from one
thread to another, how page tables look, how an interrupt arrives, how the
machine boots. When you're learning, the two are easy to mix up, because
the books and the code show them side by side.

AquilaOS keeps them apart, and so do these documents. This one explains
where the line is and how it's drawn in the code; the others describe
each subsystem's design first, the same on every machine, then how the
machine-specific part does its share, in sections whose titles say which
machine it is.

## Three layers

| Layer            | What it is                                              | Where                                   |
|------------------|---------------------------------------------------------|-----------------------------------------|
| **The kernel**   | the design: processes, memory, files, devices, networking | `kernel/core`, `mm`, `sys`, `fs`, `dev`, `net` |
| **The architecture** | the CPU: its modes, registers, page tables, traps, context switch | `kernel/arch/i386/` (`boot`, `cpu`, `mm`, `sys`) |
| **The platform** | the machine around the CPU: how it boots, its interrupt controllers, timers, firmware tables | `kernel/arch/i386/platform/` |

- **The kernel** is written in plain C against interfaces. It doesn't know
  what a page table entry looks like or how an interrupt is acknowledged.
- **The architecture** is x86, in two flavours built from the same
  directory: i386 (32-bit) and x86_64 (64-bit), with
  `#if ARCH_BITS==32` where they differ (`kernel/arch/x86_64` is a link
  to `i386/`). It's what an instruction set manual describes: privilege
  levels, the IDT, the GDT and TSS, paging, the FPU, `int $0x80`.
- **The platform** is the PC: what isn't in the CPU but every PC has. The
  8259 and APIC interrupt controllers, the PIT and HPET timers, the CMOS
  clock, the PS/2 controller, ACPI's tables, the Multiboot boot loader.
  Another x86 machine could differ here and not in the architecture: the
  Intel Quark platform (`platform/quark/`) was one; its build
  configuration has fallen behind.

Drivers sit in between. A driver in `kernel/dev/` is generic code for a
particular device (an e1000 network card, an ATA disk, an 8250 serial
port), which could be on any machine that has the device; it reaches the
hardware through the platform's I/O functions and interrupts.

## Where the line is

The kernel and the architecture meet in three ways.

### Functions the architecture provides

Every function the kernel needs from the machine is declared in
`kernel/include/core/arch.h` and called `arch_*`, or is one of the
`pmap_*` functions of `kernel/include/mm/pmap.h`. An architecture
implements all of them:

| What                        | Functions                                                         |
|-----------------------------|-------------------------------------------------------------------|
| processes and threads       | `arch_proc_init()`, `arch_proc_fork()`, `arch_thread_create()`, `arch_kthread_create()`, `arch_thread_kill()`, `arch_sys_execve()`, `arch_init_execve()` |
| running threads             | `arch_thread_spawn()`, `arch_thread_switch()`, `arch_idle()`, `arch_sleep()`, `arch_yield()`, `arch_cur_thread_kill()`, `arch_sched_init()` |
| system calls                | `arch_syscall_return()`: a call's result, into the thread's registers |
| signals                     | `arch_sigcode_map()`, `arch_sigreturn()`: the handler's frame, and back |
| page tables                 | `pmap_create()`, `pmap_switch()`, `pmap_add()`, `pmap_remove()`, `pmap_protect()`, `pmap_page_copy()`, ...; `arch_mm_setup()`, `arch_page_get_mapping()` |
| physical memory and devices | `arch_phys_map()`, `arch_mmio_map()`, `arch_msi_alloc()`          |
| user memory                 | `arch_copy_user()`, `arch_copy_user_str()`: copies that stop at a fault |
| several CPUs                | `arch_cpu_kick()`, `arch_lock_spin()`, `arch_lock_taken()`        |
| time                        | `arch_rtime_ns()` (since boot), `arch_time_get()` (the date), `arch_cycles()` |
| the machine                 | `arch_reboot()`, `arch_halt()`, `arch_poweroff()`, `arch_stack_trace()` |

The names say what the kernel wants, not how: `arch_thread_switch()`
runs another thread, and the kernel doesn't care that on x86 it means
saving registers on the kernel stack, loading `CR3` and setting the TSS's
stack pointer.

### Headers the architecture completes

Some things the kernel needs are types and constants, not functions: how
big a physical address is, where user space ends, what a CPU's
machine-specific state is. These come from headers the architecture
overrides. The include path lists the platform's headers first, then the
architecture's, then the kernel's:

```
-I kernel/arch/i386/platform/pc/include
-I kernel/arch/i386/include
-I kernel/include
```

So `#include <mm/mm.h>` finds `kernel/arch/i386/include/mm/mm.h` first,
which defines what is x86's and then includes the kernel's own with
`#include_next <mm/mm.h>`. The headers completed this way:

| Header          | What the architecture adds                                        |
|-----------------|-------------------------------------------------------------------|
| `config.h`      | `ARCH_BITS` (32 or 64), the machine's name for `uname`           |
| `core/system.h` | `PAGE_SIZE`, and how much a page table maps                       |
| `core/arch.h`   | `struct x86_thread` (a thread's registers), `USER_STACK`, `KERN_STACK_SIZE`, interrupts on and off |
| `cpu/cpu.h`     | `struct arch_cpu` (a CPU's GDT, TSS, page tables), `curcpu()`, I/O ports |
| `mm/mm.h`       | `paddr_t`, `vaddr_t`, the kernel's address space layout, `PHYS2VIRT()` |
| `mm/pmap.h`     | `struct pmap`, an address space's page tables                     |
| `boot/boot.h`   | the boot loader's information to `struct boot`                    |
| `core/panic.h`  | `panic()` (replaced whole)                                        |

The generic structures carry a pointer or a member for the architecture's
part and leave it opaque: a thread's `arch` (`struct x86_thread`), a CPU's
`arch` (`struct arch_cpu`), an address space's `pmap`.

### Functions the architecture calls

The other way round, the architecture calls into the kernel when the
machine does something:

| When                            | The kernel's function                                   |
|---------------------------------|---------------------------------------------------------|
| boot is done                    | `kmain(struct boot *)`                                  |
| a system call                   | the handler in `syscall_table[]`, by number             |
| a page fault                    | `mm_page_fault()`, with the address and what was tried  |
| the timer's tick                | `sched_timer_tick()` (the boot CPU), `sched_cpu_tick()` (the others) |
| a switch is due                 | `schedule()`                                            |
| on the way back to user mode    | `signal_take()`: a signal to deliver                    |

Everything in between is the architecture's: which vector a page fault
is, where the faulting address is found (`CR2`), what the error code
means. The kernel gets an address and "write, from user mode".

## What a new architecture needs

To run AquilaOS on another CPU, one would write a `kernel/arch/<name>/`
with:

- the headers above;
- an entry from its boot loader that sets up the CPU, turns on paging,
  fills in a `struct boot` and calls `kmain()` (see [Booting](booting.md));
- trap and interrupt entry that ends in the calls in the last table (see
  [Interrupts](interrupts.md));
- the `arch_*` and `pmap_*` functions;
- a linker script and a build configuration (`configs/`).

`kernel/arch/none/` and `kernel/arch/arm/` are placeholders for this: a
few headers, no code yet.

## Where the line is blurred

The separation is the design, but it isn't finished; these are the places
where x86 still shows through in generic code:

- **Device interrupts** have no generic interface. Drivers install their
  handlers with `x86_irq_handler_install()`, and the handlers take a
  `struct x86_regs *` (`dev/ata/ata.c`, `dev/net/e1000.c`,
  `dev/tty/uart/8250/8250.c`). The PS/2 keyboard and mouse drivers call
  the i8042's functions directly (`x86_i8042_handler_register()`).
- **The platform's interface** (`platform_init()`,
  `platform_timer_setup()`) is x86's own, in
  `kernel/arch/i386/include/core/platform.h`: the architecture calls it,
  the kernel doesn't.
- **System call restarts** are explained in `sys/syscall.c` in terms of
  x86 (`x86_return_to_user()`), though the decision is the architecture's.

When reading the code, a name tells you the layer: `arch_*` is the
interface, `x86_*` is the architecture's or platform's own, anything else
in `kernel/arch/` is too, and everything outside `kernel/arch/` is the
kernel.

## Where it is

| File                                     | What                                         |
|------------------------------------------|----------------------------------------------|
| `kernel/include/core/arch.h`             | the `arch_*` interface                       |
| `kernel/include/mm/pmap.h`               | the page table interface                     |
| `kernel/include/sys/uaccess.h`           | user memory, over `arch_copy_user()`         |
| `kernel/include/sys/cpu.h`               | `struct cpu`, with the architecture's part   |
| `kernel/include/boot/boot.h`             | `struct boot`: what the kernel gets at boot  |
| `kernel/arch/i386/include/`              | x86's headers                                |
| `kernel/arch/i386/{boot,cpu,mm,sys}/`    | x86: boot, CPU, paging, threads and traps    |
| `kernel/arch/i386/platform/`             | the PC (`pc/`, `misc/`)                      |
| `kernel/Makefile`                        | the include path                             |
| `configs/`                               | the build configurations                     |

## References

- *Lions' Commentary on UNIX 6th Edition*, John Lions: Sixth Edition UNIX
  had the same split, a little assembly (`m40.s`) under portable C.
- *The Design and Implementation of the FreeBSD Operating System*,
  McKusick, Neville-Neil and Watson: the machine-dependent and
  machine-independent parts of a BSD kernel, and the `pmap` interface,
  which AquilaOS's is named after.
- NetBSD's [`pmap(9)`](https://man.netbsd.org/pmap.9) manual page: the
  same interface, documented in full.
