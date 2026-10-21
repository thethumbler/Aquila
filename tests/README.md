# Kernel tests

Boots the kernel in QEMU with small test programs as `/init` and checks
what they report on the serial port.

```
tests/run.sh            # build the kernel and run everything
tests/run.sh cpu ext2   # run some tests
tests/run.sh -n         # reuse the kernel image from the last build
tests/run.sh -v         # print the serial log of failed tests
tests/run.sh -a i386    # the i386 kernel and system (default: x86_64)
```

The kernel is built with the host compiler through
[`host/x86_64-pc.mk`](host/x86_64-pc.mk) (or
[`host/i386-pc.mk`](host/i386-pc.mk)), the regular configuration with
`gcc`/`ld` instead of the cross toolchain. Any gcc with 32-bit support
works. The libc, system, disk and tcc tests use the system built with the
cross toolchain (`make`, `make CONFIG=i386-pc`) and are skipped without
it.

Requirements: `gcc`, `ld`, `cpio`, `qemu-system-x86_64` (and
`qemu-system-i386` for i386), and for the ext2 test `mke2fs`, `e2fsck`,
`debugfs` and `python3`, for the disk test `sfdisk`, `mkfs.fat`, mtools
and `fakeroot` (skipped if missing). Build output goes to
`tests/out/x86_64` (`tests/out` for i386).

## Tests

| test       | what it checks |
|------------|----------------|
| `ata`      | a disk on the first ATA channel is detected, over several boots |
| `cpu`      | DF and segment registers are sanitized on kernel entry, user exceptions and port I/O raise signals, SSE works, entry stack alignment |
| `syscalls` | kernel addresses are rejected (`EFAULT`), descriptor and signal checks, wait, threads, pipes (and how fast a ping-pong through them goes: no timer tick waited for), unix sockets, bad ELF files, descriptor flags and close-on-exec, symlinks, signal masks, process groups, copy-on-write after fork, the `*at()` calls, links, rename, permissions and attributes, `SIGPIPE`, `nanosleep`, clocks and `times` |
| `memory`   | with 6 GiB of RAM (2 GiB on i386, which uses 768 MiB), one big mapping (1 GiB, 128 MiB on i386) then 4.5 GiB (512 MiB) of anonymous memory filled, every page with where it is, and read back: memory above 4 GiB, the kernel's small objects (slabs) and its hashmaps for millions of pages |
| `net`      | the e1000 and IPv4: a peer on the host (`net/peer.py`, QEMU's `-netdev socket`) checks the answers to ARP, pings (100 of them, around the rings) and a datagram to a closed port; the guest's program checks process 0 and its network thread (`[net]`, which takes the frames up the stack) in `/proc/0/status`, UDP and ping sockets over loopback (binding, `getsockname`, `FIONREAD`, timeouts, `SO_ERROR` from "port unreachable", `SO_BROADCAST`) and the interfaces' `SIOCGIF*` requests, then talks to the peer: UDP after resolving its address with ARP, a ping, a refused datagram, one through the gateway and a broadcast |
| `ext2`     | writes through singly and doubly indirect blocks and appends on an ext2 image, then `e2fsck` and a byte compare on the host |
| `libc`     | errno and signal numbers, stdio, directories, fork/wait, crypt, passwd/shadow and group, truncate, signal masks, the `*at()` calls, `realpath`, `ttyname`, sleeping, `SIGPIPE` and C99 `printf` formats through newlib (needs `make toolchain`) |
| `system`   | the initrd built from source (with contrib/, without ports/): logins on the serial console, then dash runs commands, pipelines, `$(...)`, a long line, `^C`, `/dev/tty` and sbase tools, and the `dhcp` service enabled (sdhcp gets the address from QEMU's user network, a ping goes through) (needs `make toolchain` and `make`) |
| `disk`     | the system installed on a hard disk image (`mk/hdd.sh`): booted from it with Limine twice, a file written on the first boot is read on the second (`mk/hdd-kernel.sh` and `mk/hdd-update.sh` put the kernel and an updated system on it in between), `e2fsck` is clean (needs `make toolchain` and `make`) |
| `tcc`      | the tcc port (`make ports`) on AquilaOS: compiles and runs programs against the C library and libm (skipped without the port) |

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
