# System calls

A program in user mode can't touch the hardware, other processes'
memory, or the kernel's. When it needs any of that (to open a file, read
from a terminal, start a process, get the time) it asks the kernel, with a
*system call*: a deliberate trap into the kernel, with a number that says
which service it wants and a few arguments. The kernel does the work, with
its own privileges, and returns a result to the program, which carries on
after the trap as if it had called a function.

System calls are the kernel's whole interface to user space: everything a
program can do beyond computing is one of them. AquilaOS has about ninety,
the usual UNIX ones (`open`, `read`, `fork`, `execve`, `waitpid`,
`sigaction`, `socket`, `mmap`, ...), and the C library (newlib) turns them
into the C functions programs know.

How the trap is made, and where the number and the arguments are, is the
architecture's business. What happens next is not: finding the handler,
reading user memory safely, returning errors, sleeping. This document
starts with the kernel's side, the same on every machine, then describes
x86's (see [The kernel and the machine](architecture.md) for the split).

## The kernel's side

### The table

Every system call has a number. The numbers are defined once, in
[`include/uapi/aquila/syscall.h`](../../include/uapi/aquila/syscall.h),
a header shared by the kernel and the C library: the *user API* headers
in `include/uapi/` are what the toolchain installs into the system's
`/usr/include` (see [Building and testing](building.md)), so both sides
are compiled against the same numbers and structures.

```c
#define SYS_exit            1
#define SYS_close           2
#define SYS_execve          3
#define SYS_fork            4
...
#define SYS_sigreturn       95
```

The kernel's handlers are the `sys_*` functions of
[`sys/syscall.c`](../../kernel/sys/syscall.c), and `syscall_table[]` maps
the numbers to them:

```c
void (*syscall_table[])() = {
    [SYS_exit]          = sys_exit,
    [SYS_close]         = sys_close,
    [SYS_execve]        = sys_execve,
    ...
};
```

A number with no handler (past the table's end, or a hole in it) returns
`ENOSYS`. Two numbers are holes on purpose: 35 (`auth`, removed: `login`
checks `/etc/shadow` itself) and 58 (`utime`, replaced by `utimensat`). A
number is never reused, so an old program can't call the wrong thing.

### Arguments and results

A call takes up to five arguments, each the size of a word: integers,
or pointers into the program's memory. A call that needs more takes a
pointer to a structure instead, defined in the same header:
`mmap`'s `struct aquila_mmap_args`, `mount`'s, the sockets' `send` and
`recv` (`struct aquila_socket_io_args`), `select`'s, `thread_create`'s.

The architecture finds the number and the arguments, and calls the
handler as a C function: `sys_read(fd, buf, nbytes)`.

The result is one word, given back with `arch_syscall_return(thread,
value)`, which puts it where the architecture returns it to user mode.
By the kernel's convention (see the [README](README.md))
an error is a negative `errno`: `-EBADF`, `-EFAULT`. The C library turns
that into the C convention, `-1` and `errno` set (`__syscall_ret()`, in
[`libc/aquila/syscall.h`](../../libc/aquila/syscall.h)).

```c
static void sys_read(int fildes, void *buf, size_t nbytes)
{
    struct file *file = fd_file(fildes);

    if (!file) {
        arch_syscall_return(curthread, -EBADF);
        return;
    }

    if (uaccess_check(buf, nbytes)) {
        arch_syscall_return(curthread, -EFAULT);
        return;
    }

    struct iobuf io;
    iobuf_init(&io, buf, nbytes, 1);

    ssize_t ret = vfs_file_read(file, &io);
    arch_syscall_return(curthread, ret);
}
```

The handlers return nothing; they set the result with
`arch_syscall_return()`. That looks roundabout, but some calls return to
more than one thread, or not to the caller at all: `fork` returns the
child's pid to the parent and 0 to the child, a thread that hasn't run
yet ([Processes and threads](processes.md)); `execve` doesn't return
when it works; `exit` never does; `sigreturn` puts back the registers a
signal handler interrupted ([Signals](signals.md)). Naming the thread
covers them all. The cost is that every error path in `sys/syscall.c`
is three lines, not one.

A call that returns something bigger than a word (`mmap`'s address and
an error, `pipe`'s two descriptors, `stat`'s structure) writes it to user
memory, through a pointer the program passed.

### User memory

Many arguments are pointers: the buffer `read` fills, the path `open`
takes, the structure `stat` fills. The kernel can't trust them. A pointer
from user space may be:

- **wrong**: not mapped at all, or read-only where the kernel would
  write. If the kernel used it, it would fault, in the kernel;
- **malicious**: an address in the kernel's half of the address space.
  The kernel can reach its own memory, and would read or overwrite it on
  the program's behalf;
- **changing**: another thread of the process can change the memory
  while the kernel looks at it, so a path checked once may be another one
  when it's used.

So the kernel **never dereferences a user pointer**. It copies what it
needs in, and what it gives out, with
[`include/sys/uaccess.h`](../../kernel/include/sys/uaccess.h):

| Function                         | What                                                     |
|----------------------------------|----------------------------------------------------------|
| `copyin(kdst, usrc, n)`          | `n` bytes from user memory into the kernel's: 0, or `-EFAULT` |
| `copyout(udst, ksrc, n)`         | `n` bytes from the kernel's out to user memory           |
| `copyinstr(kdst, usrc, max)`     | a string, its length; `-ENAMETOOLONG` if it doesn't fit in `max` |
| `copyin_upto()`, `copyout_upto()` | how many bytes went before a fault (a `read` into a buffer that runs into unmapped memory fills it up to there) |
| `uaccess_check(ptr, n)`          | the range check alone                                    |

Each first checks that the range is user space's (below `USER_STACK`, the
architecture's limit), then copies with `arch_copy_user()`, which stops
at a fault instead of panicking. Then the kernel works on its own copy:
a path is copied once (`upath_get()`, at most 1024 bytes, newlib's
`PATH_MAX`) and looked up from the copy; a structure is copied in before
its fields are read.

User memory is mapped in the kernel's address space while a process
runs (it's the same page tables), so a copy is a plain memory copy. A
fault during one goes to the page fault handler first, which maps the
page if it's part of the process's memory, not yet there (a page of the
heap never touched, a page of a file): the copy carries on. Only when the
address isn't the process's at all does the copy stop and return
`-EFAULT` (see [Interrupts](interrupts.md)). Anywhere else, a
kernel fault at a user address is a bug, and the kernel panics: it
touched user memory without the copy functions.

Reads and writes, whose data can be large, use an *I/O buffer*
(`struct iobuf`, [`include/fs/iobuf.h`](../../kernel/include/fs/iobuf.h))
instead of a kernel copy of the whole: the filesystem or the device copies
to and from the user's buffer as it goes, through `iobuf_in()` and
`iobuf_out()`, which use the same copies. An I/O buffer can hold several
segments, but the kernel has no vectored calls yet: the C library's
`readv` and `writev` gather the vectors into one buffer and make one
`read` or `write`.

### The open files a call uses

A call on a descriptor looks the open file up (`fd_file()`) and *holds*
it (`file_hold()`, [`fs/file.c`](../../kernel/fs/file.c)): it takes a
reference, recorded in the thread. When the call returns, the thread lets
go of them all (`thread_files_release()`). Without that, another thread
of the process could `close()` the descriptor while the call sleeps in a
`read`, and the file would be freed under it. A thread killed in the
middle of a call lets go of its files the same way. `select` and `poll`,
which may name hundreds of descriptors, only peek at them (`fd_peek()`)
and look again after each sleep.

### Calls that sleep

A call that has to wait (a `read` with no data yet, `waitpid`, `accept`,
`nanosleep`) sleeps in the kernel, on the thread's own kernel stack,
until it's woken; other threads run meanwhile ([Processes and
threads](processes.md)). A signal can end the sleep. The call then
returns `-ERESTARTSYS` or `-ERESTARTNOHAND`, values only the kernel sees
([`include/sys/signal.h`](../../kernel/include/sys/signal.h)): on the
way out, the call is made again from the start if no handler runs (or
the handler has `SA_RESTART`, for `-ERESTARTSYS`), otherwise the program
gets `EINTR`. See [Signals](signals.md).

### On the way out

When the handler is done, before returning to user mode:

1. the open files the call held are let go of;
2. an *urgent* thread woken during the call (the network's, by a packet
   sent to ourselves) runs first;
3. the signals that came are delivered, and an interrupted call is
   restarted or made to fail with `EINTR`.

A system call runs like the rest of the kernel: with interrupts off, and
holding the kernel lock if there are several CPUs ([Interrupts](interrupts.md),
[SMP and the kernel lock](smp.md)).

### Adding a system call

On the kernel's side:

1. a number in `include/uapi/aquila/syscall.h`, the next free one, and a
   structure there if it needs more than five arguments;
2. its handler, `sys_<name>()`, in `kernel/sys/syscall.c`: arguments
   checked, user memory copied in and out, the result with
   `arch_syscall_return()`;
3. its entry in `syscall_table[]`.

On the C library's side, in `libc/aquila/` (newlib's port, see
[Building and testing](building.md)):

1. the C function, usually in `syscalls.c`, with the `SYSCALL()` or
   `SYSCALL5()` macros of `syscall.h`, which make the trap and turn a
   negative result into `errno`:

   ```c
   int close(int fildes)
   {
       return SYSCALL(SYS_close, fildes, 0, 0);
   }
   ```

2. its declaration, if newlib's headers don't have it (`libc/aquila/include/`,
   `libc/aquila/sys/`); a new source file goes in `Makefile.inc`;
3. the headers and libc built again: `toolchain/build.sh headers libc`
   (`ARCH=i386` for i386), then the programs that use it.

And a check of it in `tests/syscalls/init.c`, which calls the kernel
directly, without the C library.

## x86: int $0x80

On x86, a program makes a system call with `int $0x80`, a software
interrupt through the IDT's gate `0x80`, the only gate user mode may raise
(see [Interrupts](interrupts.md)). The CPU
switches to the kernel's stack and privilege level, and the entry stubs
save the registers in a `struct x86_regs` on the kernel stack, as for any
interrupt.

### Registers

The number goes in `eax`, the arguments in five more registers, and the
result comes back in `eax`:

| | Number | Arguments 1 – 5                  | Result |
|----------|--------|-----------------------------------|--------|
| i386     | `eax`  | `ebx`, `ecx`, `edx`, `esi`, `edi` | `eax`  |
| x86_64   | `rax`  | `rdi`, `rsi`, `rdx`, `r10`, `r8`  | `rax`  |

i386's are the registers Linux uses for its own `int $0x80`. x86_64's are
the ones Linux uses with the `syscall` instruction (`r10` for the fourth,
as `syscall` itself overwrites `rcx`), which are the C calling
convention's but for that one: the C library's wrappers pass their
arguments on with no shuffling.

### From the trap to the handler

`__x86_isr()` ([`cpu/isr.c`](../../kernel/arch/i386/cpu/isr.c)) sees
vector `0x80`, remembers where the registers are (`arch->regs`), and calls
`arch_syscall()` ([`sys/syscall.c`](../../kernel/arch/i386/sys/syscall.c)),
which:

1. looks the number up in `syscall_table[]` (`ENOSYS` if there's none),
   and keeps it in `curthread->syscall_nr`;
2. calls the handler with the five argument registers;
3. on the way out: the held files, an urgent thread (`x86_preempt()`),
   then the signals (`x86_return_to_user()`, [`sys/signal.c`](../../kernel/arch/i386/sys/signal.c)).

`arch_syscall_return()` writes the result into the saved `eax` (`rax`),
which the return from the interrupt loads. A thread that hasn't run yet,
a child of `fork`, has no saved registers on its stack: its result goes in
the thread's structure (`arch->eax`), from where it starts.

A call to restart is restarted by moving the saved instruction pointer
back by two bytes, the length of `int $0x80`, and putting the number back
in `eax`: when the thread returns to user mode, it makes the same call
again, with the same arguments, which are still in their registers.

### Copying user memory

`arch_copy_user()` and `arch_copy_user_str()`
([`cpu/uaccess.S`](../../kernel/arch/i386/cpu/uaccess.S)) are a `rep movsb`
and a byte loop. The instructions in them that touch user memory are
listed in a table, `x86_uaccess_fixups`, each with where to go on if it
faults. When a page fault in the kernel can't be resolved,
`uaccess_fixup()` looks the faulting instruction up there; if it's one of
them, the fault "returns" to its fixup, which returns what wasn't copied
(or `-EFAULT`). Linux calls this the *exception table*.

### Shortcuts

- **No fast entry.** x86 has instructions made for system calls,
  `sysenter` (i386) and `syscall` (x86_64), which skip most of what an
  interrupt does and are several times faster. AquilaOS uses `int $0x80`
  on both: one entry path for everything.
- **No SMAP.** Recent x86 CPUs can forbid the kernel to touch user memory
  except in marked places (*Supervisor Mode Access Prevention*), which
  turns a forgotten copy function into a fault at once. AquilaOS relies
  on the rule instead: a kernel fault at a user address outside the
  copies panics, but a read that happens to succeed goes unnoticed.

## Where it is

| File                                         | Layer    | What                                    |
|----------------------------------------------|----------|-----------------------------------------|
| `include/uapi/aquila/syscall.h`              | kernel   | the numbers and argument structures, shared with libc |
| `kernel/sys/syscall.c`                       | kernel   | the handlers, `syscall_table[]`         |
| `kernel/include/sys/syscall.h`               | kernel   | the table's declaration                 |
| `kernel/include/sys/uaccess.h`               | kernel   | `copyin()`, `copyout()`, `copyinstr()`  |
| `kernel/include/fs/iobuf.h`, `kernel/fs/iobuf.c` | kernel | I/O buffers in user memory           |
| `kernel/fs/file.c`                           | kernel   | `file_hold()`, `thread_files_release()` |
| `kernel/include/sys/signal.h`                | kernel   | `ERESTARTSYS`, `ERESTARTNOHAND`         |
| `kernel/arch/i386/cpu/isr.c`                 | x86      | the trap, `uaccess_fixup()`             |
| `kernel/arch/i386/sys/syscall.c`             | x86      | `arch_syscall()`, `arch_syscall_return()` |
| `kernel/arch/i386/sys/signal.c`              | x86      | the way back to user mode, restarts     |
| `kernel/arch/i386/cpu/uaccess.S`             | x86      | `arch_copy_user()`, the fixup table     |
| `libc/aquila/syscall.h`, `syscalls.c`        | libc     | the traps and the C functions           |
| `tests/syscalls/init.c`                      | test     | the calls, without libc                 |

## References

- *The Design of the UNIX Operating System*, Maurice J. Bach, chapter 6.4:
  the system call interface.
- *Lions' Commentary on UNIX 6th Edition*, John Lions: `trap` and the
  `sysent` table, the first of its kind.
- The Linux kernel's `Documentation/process/adding-syscalls.rst`, and its
  `syscall(2)` manual page: the registers of each architecture.
- Intel® 64 and IA-32 Architectures Software Developer's Manual, Volume
  3: *Interrupt and Exception Handling*, and the `SYSCALL`, `SYSENTER`
  and `INT n` instructions (Volume 2); SMAP in *Paging* (Access Rights).
- FreeBSD's `copyin(9)` manual page: the same functions, the same names.
