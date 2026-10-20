# Kernel tests

Boots the kernel in QEMU with small test programs as `/init` and checks
what they report on the serial port.

```
tests/run.sh            # build the kernel and run everything
tests/run.sh cpu ext2   # run some tests
tests/run.sh -n         # reuse the kernel image from the last build
tests/run.sh -v         # print the serial log of failed tests
```

The kernel is built with the host compiler through
[`host/i386-pc.mk`](host/i386-pc.mk), the regular `i386-pc` configuration
with `gcc`/`ld` instead of the `i686-aquila` cross toolchain. Any gcc with
32-bit support works.

Requirements: `gcc`, `ld`, `cpio`, `qemu-system-i386`, and for the ext2
test `mke2fs`, `e2fsck`, `debugfs` and `python3` (skipped if missing).
Build output goes to `tests/out/`.

## Tests

| test       | what it checks |
|------------|----------------|
| `ata`      | a disk on the first ATA channel is detected, over several boots |
| `cpu`      | DF and segment registers are sanitized on kernel entry, user exceptions and port I/O raise signals, SSE works, entry stack alignment |
| `syscalls` | kernel addresses are rejected (`EFAULT`), descriptor and signal checks, wait, threads, pipes, unix sockets, bad ELF files |
| `ext2`     | writes through singly and doubly indirect blocks and appends on an ext2 image, then `e2fsck` and a byte compare on the host |

A test fails on a `FAIL` line, a kernel panic, a guest reset (QEMU runs
with `-no-reboot`) or if it doesn't finish in time.

## Writing a test

Add `tests/<name>/init.c` using [`lib/aqtest.h`](lib/aqtest.h): no libc,
syscalls through `SC()`, results through `check()`.

```c
#include "../lib/aqtest.h"

void aq_main(void)
{
    aq_begin("example");    /* mounts devfs, opens /dev/ttyS0 */

    check("getpid", SC(S_GETPID, 0, 0, 0) > 0, 1);

    aq_end();               /* prints the summary, never returns */
}
```

Then add a `test_<name>` function and the name to `ALL_TESTS` in
`run.sh`. The initrd has `/init`, and empty `/dev`, `/mnt` and `/tmp`
directories; the initramfs is read-only, mount tmpfs to create files.
