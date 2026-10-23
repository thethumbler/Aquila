# Interrupts

An interrupt makes the CPU stop what it is running and jump to a handler,
then return to it. Devices use interrupts to say "something happened" (a
key was pressed, a packet arrived, a disk request finished), the CPU uses
them to report faults (a page that isn't mapped, a division by zero), the
timer uses one to give the scheduler a chance to switch threads, and
programs use one to call the kernel. On x86 they all go through the same
mechanism: a number, the *vector*, and a table that says where each
vector's handler is.

This document covers that mechanism on AquilaOS: the vectors and the
table, the path from the CPU's entry to the C handlers, the interrupt
controllers that bring device interrupts to the CPU (the 8259 PIC, the
APIC, MSI), how an interrupt is dispatched, and how interrupts fit with
the scheduler and with several CPUs.

## The x86 side

### Vectors and the IDT

There are 256 vectors. The first 32 are the CPU's own *exceptions*
(vector 14 is a page fault, 13 a general protection fault, and so on);
the rest are free for the system to use. The *Interrupt Descriptor
Table* (IDT) has an entry, a *gate*, for each vector: the address of its
handler, the code segment to run it in, and the privilege level allowed
to raise it with an `int` instruction.

AquilaOS uses *interrupt gates* everywhere: the CPU clears the interrupt
flag (`IF`) when it enters through one, so a handler isn't interrupted by
another interrupt. The system call gate (vector `0x80`) is the only one
user mode may raise itself; the others are raised by the CPU or by the
interrupt controllers.

AquilaOS's vectors:

| Vector      | What                                                         |
|-------------|--------------------------------------------------------------|
| 0 – 31      | CPU exceptions                                               |
| 32 – 79     | device interrupts, IRQ 0 to 47 (vector = 32 + IRQ)           |
| `0x80`      | system calls (`int $0x80`)                                   |
| `0xF0`      | the local APIC's timer (the scheduler's tick)                |
| `0xF1`      | IPI: flush the TLB (SMP)                                     |
| `0xF2`      | IPI: wake up and enter the kernel (SMP)                      |
| `0xFF`      | the local APIC's spurious interrupt                          |

The IDT is one table, shared by every CPU; it is filled in by
`x86_idt_setup()` and `x86_isr_setup()` (exceptions, the system call) and
`x86_irq_gates_setup()` (the rest).

### What the CPU does on entry

When an interrupt comes, the CPU pushes the interrupted context's
instruction pointer, code segment and flags; if it came from user mode it
first switches to the kernel stack named in the *task state segment*
(TSS) and also pushes the user stack pointer and stack segment. Some
exceptions push an *error code* after those (a page fault's says whether
it was a read or a write, from user or kernel mode). Then it jumps to the
gate's handler. `iret` undoes it.

## From the gate to C

Every vector's gate points to a small stub in
[`cpu/cpu.S`](../../kernel/arch/i386/cpu/cpu.S). The stubs make every
interrupt look the same to C:

1. The stub pushes the error code (a 0 if the CPU pushed none) and the
   vector number, then jumps to the common code: `isr_handler` for
   exceptions and system calls, `irq_stub` for the rest.
2. The common code saves the general registers (`push_context`) and
   prepares the kernel's environment (`kernel_entry`): the direction flag
   is cleared, and on i386 the data segment registers are loaded, `%gs`
   with the CPU's own segment. On x86_64, an interrupt from user mode
   swaps the GS base with `swapgs` first. Either way `%gs:0` then points
   to the running CPU's `struct cpu`, which is what `curcpu()` and
   `curthread` read.
3. It calls the C handler with a pointer to what is now on the stack, a
   `struct x86_regs`: the registers, the vector and error code, and what
   the CPU pushed. The handler can read and change the interrupted
   context through it (a system call's return value goes into its `eax`).
4. On the way back the same steps are undone in reverse, and `iret`
   returns to the interrupted code (or, after a thread switch, to another
   thread's: see below).

The C handlers are `__x86_isr()` for exceptions and system calls
([`cpu/isr.c`](../../kernel/arch/i386/cpu/isr.c)) and `__x86_irq_handler()`
for device interrupts and IPIs
([`platform/misc/irq.c`](../../kernel/arch/i386/platform/misc/irq.c)).

## Interrupt controllers

Devices don't talk to the CPU directly: an *interrupt controller* takes
their interrupt lines and turns them into vectors. A PC has had two kinds,
and AquilaOS supports both behind one interface, `struct x86_irq_chip`:

```c
struct x86_irq_chip {
    const char *name;
    void (*mask)(unsigned irq);     /* stop this IRQ */
    void (*unmask)(unsigned irq);   /* let it through */
    void (*eoi)(unsigned irq);      /* the handler is done with it */
    const char *(*describe)(unsigned irq);
    const char *timer;
};
```

An IRQ is unmasked when its first handler is installed, masked when its
handlers are removed, and acknowledged (*end of interrupt*, EOI) after its
handlers ran.

### The 8259 PIC

The original PC's controller, two 8259s chained together, has 16 lines:
the *ISA IRQs*, 0 to 15 (0 the PIT, 1 the keyboard, 4 the first serial
port, 12 the PS/2 mouse, 14 and 15 the ATA channels). The second 8259 is
connected to the first one's line 2.

At boot the 8259s send their interrupts to vectors 8 to 15 and `0x70` to
`0x77`, which collide with the CPU's exceptions. AquilaOS *remaps* them to
32–47 ([`platform/misc/pic.c`](../../kernel/arch/i386/platform/misc/pic.c)),
so that IRQ *n* is vector 32 + *n*. An EOI is a command to the 8259 that
raised it (both, for the second one's lines).

The 8259 is used when there is no APIC, or when the kernel is booted with
`apic=off`. With the APIC, the 8259s are remapped anyway (a stray
interrupt from one lands on a harmless vector) and then masked.

### The APIC

Every CPU since the Pentium has a *local APIC*, and chipsets have one or
more *I/O APICs*
([`platform/misc/apic.c`](../../kernel/arch/i386/platform/misc/apic.c)).

- The **local APIC** is the CPU's own: it receives interrupts for its CPU,
  has a timer, and sends and receives *inter-processor interrupts* (IPIs).
  Its registers are memory-mapped (at `0xFEE00000` usually). Each CPU's is
  enabled by `x86_lapic_cpu_setup()`; it is acknowledged by writing its
  EOI register.
- An **I/O APIC** has a number of input *pins*, numbered system-wide as
  *global system interrupts* (GSIs), and a *redirection entry* for each:
  which vector to send, to which CPU, whether the line is edge- or
  level-triggered, active high or low, masked or not.

Where the APICs are, and how the ISA IRQs map to pins, come from ACPI's
**MADT** table (signature `APIC`). By default ISA IRQ *n* is GSI *n*, but
the MADT has *interrupt source overrides* for the exceptions: on most
machines the PIT's IRQ 0 is wired to pin 2, and some lines are
level-triggered. `x86_apic_setup()` reads the table, enables the local
APIC, masks every pin, and installs the APIC as the system's
`x86_irq_chip`. From then on IRQ *n* is the pin the MADT says it is
(`apic_route()`), sent to the boot CPU with vector 32 + *n*. A PCI
device's IRQ is its configuration space's *interrupt line*, an ISA IRQ
number, which QEMU's chipsets wire to the matching pins (level-triggered,
as the MADT's overrides say). A machine whose PCI lines are only on the
I/O APIC's pins past 15 would need ACPI's `_PRT` table, which AquilaOS
doesn't read; a device with MSI (below) doesn't need it.

### MSI

A PCI device that supports *message signalled interrupts* doesn't need a
line at all: it raises an interrupt by writing a value (the *data*) to an
address (the *message address*), which the local APIC receives. The
address names the CPU, the data the vector.

`pci_msi_enable()` ([`dev/pci/pci.c`](../../kernel/dev/pci/pci.c)) asks
the architecture for an IRQ and its message (`arch_msi_alloc()`), writes
them to the device's MSI capability and turns the device's line off. The
IRQs for messages are allocated above the I/O APICs' pins
(`x86_irq_alloc()`), each with a controller of its own, `msi_chip`, whose
EOI is the local APIC's. The e1000 driver uses MSI when the device has it
(QEMU's `e1000e`) and its line otherwise (`e1000`). With the 8259 there are
no messages: `arch_msi_alloc()` fails and the driver falls back to the
line.

## Dispatching an interrupt

`__x86_irq_handler()` handles every vector from 32 on:

1. The IRQ is the vector minus 32 (the local APIC's timer, vector `0xF0`,
   is an IRQ of its own: `X86_IRQ_TIMER`).
2. Each handler installed for the IRQ runs (`x86_irq_handler_install()`).
   A line can be shared, as PCI lines are: every handler looks at its
   device and does nothing if the interrupt wasn't its.
3. The controller is told it's done (EOI). For a level-triggered line this
   must come after the device was serviced, or the line, still asserted,
   would raise the interrupt again at once.
4. Then whatever the interrupt made runnable may run:
   - the **tick**, the timer's interrupt, asked for a switch: the running
     thread goes back to the ready queue and the next one runs
     (`x86_preempt()`), or, on an idle CPU, a ready thread runs;
   - an interrupt on an idle CPU woke a thread up (a disk request
     finished): it runs now, not at the next tick (`sched_idle_wakeup()`);
   - an *urgent* thread was woken (the network's, by a packet): it runs
     before the interrupted user thread goes on.
5. On the way back to user mode, the signals that came meanwhile are
   delivered (`x86_return_to_user()`).

A thread switch happens inside the handler, on the interrupted thread's
kernel stack: the thread's registers are saved there, and the stack is
left as it is until the thread runs again and returns through the rest of
the handler. Because the controller was told it's done before the switch,
the other interrupts keep coming meanwhile.

The number of times each IRQ came is kept, and `/proc/interrupts` shows
them with the controller and each IRQ's route:

```
controller: apic
timer: 2551 lapic
1: 1 ioapic gsi 1 edge
11: 140 ioapic gsi 11 level
24: 33 msi
```

## The timer

The scheduler's tick is an interrupt every 2 ms
([`sys/sched.c`](../../kernel/arch/i386/sys/sched.c), `arch_sched_init()`):

- With the APIC, it's the **local APIC's timer**, periodic, on vector
  `0xF0`. Its rate depends on the CPU's bus clock, which nothing tells the
  kernel: it is *calibrated*, counted against a clock the kernel knows
  (the TSC or the HPET, or the PIT's channel 2) for 20 ms
  (`timer_calibrate()`).
- With the 8259, it's the **PIT**, the 8254's channel 0, on IRQ 0.

On every CPU the tick charges the running process its time and asks for a
switch; the boot CPU's also keeps the time, runs the timeouts (callouts)
and wakes the threads whose sleep is over.

## Interrupts and the kernel

### Interrupts are off in the kernel

The kernel runs with interrupts disabled: the gates clear `IF`, and
nothing in the kernel sets it again, except the idle loop, which waits
for the next interrupt with `sti; hlt`. A system call, or an interrupt,
runs to completion (or to a sleep) without being interrupted. This is
what kept the kernel simple on one CPU: nothing else could run in the
kernel meanwhile.

### Several CPUs

With several CPUs, the *kernel lock* keeps that true: an
interrupt or exception that comes from user mode or from the idle loop
takes it (`kernel_enter()`), and lets it go on the way back. Device
interrupts all go to the boot CPU; the other CPUs get their own timer's
tick and two IPIs:

- **TLB flush** (`0xF1`): a CPU that changed a process's page tables asks
  the CPUs that have them loaded to flush their TLB, and waits until they
  have. This one is handled *without* the kernel lock: the CPU that sent it
  holds the lock while it waits. A CPU spinning for the lock with
  interrupts off does the flush from its spin loop instead.
- **Wake** (`0xF2`): a thread became ready and this CPU is idle, or a
  thread running here has a signal waiting or was killed: enter the
  kernel now rather than at the next tick.

A thread killed while it runs on another CPU frees itself the next time
that CPU enters the kernel. If it enters through a device interrupt, the
interrupt is handled and acknowledged first: a local APIC that never gets
its EOI blocks every vector of the same or lower priority, the CPU's own
tick and IPIs included.

## Exceptions

`__x86_isr()` handles the CPU's exceptions and the system call:

- A **page fault** (14) goes to the memory manager (`arch_mm_page_fault()`),
  which maps the page if the address is part of the process's memory
  (demand paging, copy on write). Otherwise, from user mode the process gets
  `SIGSEGV`; from the kernel copying to or from user memory
  (`copyin()`/`copyout()`, [`cpu/uaccess.S`](../../kernel/arch/i386/cpu/uaccess.S))
  the copy stops and returns `EFAULT`; anywhere else it's a kernel bug,
  and the kernel panics.
- **Device not available** (7) is how the FPU is switched lazily: a
  thread's first FPU instruction after a switch traps, and the FPU is given
  its state then (`x86_fpu_trap()`).
- **`int $0x80`** is a system call (`arch_syscall()`).
- Any other exception from user mode is a signal to the process (a
  division by zero is `SIGFPE`, an invalid instruction `SIGILL`, a general
  protection fault `SIGSEGV`); from the kernel, a panic.

## Where it is

| File                                         | What                                    |
|----------------------------------------------|-----------------------------------------|
| `kernel/arch/i386/cpu/idt.c`                 | the IDT and its gates                   |
| `kernel/arch/i386/cpu/cpu.S`                 | the entry stubs, the context switch     |
| `kernel/arch/i386/cpu/isr.c`                 | exceptions and the system call          |
| `kernel/arch/i386/platform/misc/irq.c`       | IRQs: handlers, dispatch, `/proc/interrupts` |
| `kernel/arch/i386/platform/misc/pic.c`       | the 8259 PIC                            |
| `kernel/arch/i386/platform/misc/apic.c`      | the local and I/O APICs, MSI, IPIs      |
| `kernel/arch/i386/platform/misc/pit.c`       | the PIT (8254)                          |
| `kernel/arch/i386/platform/pc/init.c`        | which controller and timer are used     |
| `kernel/arch/i386/sys/sched.c`               | the tick                                |
| `kernel/arch/i386/mm/tlb.c`                  | the TLB flush IPI                       |
| `kernel/dev/pci/pci.c`                       | MSI on PCI devices                      |

Both i386 and x86_64 build from `kernel/arch/i386/`; where they differ,
it's `#if ARCH_BITS==32` and `#else`.

## References

- Intel® 64 and IA-32 Architectures Software Developer's Manual, Volume 3:
  *Interrupt and Exception Handling* (the IDT, gates, what the CPU pushes,
  the exceptions and their error codes) and *Advanced Programmable
  Interrupt Controller (APIC)* (the local APIC, its timer, IPIs, MSI's
  address and data format).
- *82093AA I/O Advanced Programmable Interrupt Controller (IOAPIC)*,
  Intel: the I/O APIC's registers and redirection entries.
- *ACPI Specification*, section 5.2.12: the MADT and its entries.
- *8259A Programmable Interrupt Controller*, Intel: the initialization
  command words used to remap it.
- *PCI Local Bus Specification*, section 6.8: Message Signaled Interrupts.
- The OSDev wiki's pages on the [IDT](https://wiki.osdev.org/Interrupt_Descriptor_Table),
  the [8259 PIC](https://wiki.osdev.org/8259_PIC),
  the [APIC](https://wiki.osdev.org/APIC) and the
  [I/O APIC](https://wiki.osdev.org/IOAPIC).
