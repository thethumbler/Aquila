# Kernel tests

Boots the kernel in QEMU with small test programs as `/init` and checks
what they report on the serial port.

```
tests/run.sh            # build the kernel and run everything
tests/run.sh cpu ext2   # run some tests
tests/run.sh -n         # reuse the kernel image from the last build
tests/run.sh -v         # print the serial log of failed tests
tests/run.sh -a i386    # the i386 kernel and system (default: x86_64)
tests/run.sh -k apic=off    # more for the kernel's command line, every boot
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
| `syscalls` | kernel addresses are rejected (`EFAULT`), and user memory that isn't mapped (the process goes on, a read's bytes stay, a buffer running into it is used up to it), descriptor and signal checks, wait, threads, pipes (and how fast a ping-pong through them goes: no timer tick waited for), unix sockets, bad ELF files, descriptor flags and close-on-exec, open files shared by `dup` and `fork` (offset, status flags) and kept open by a read waiting on one while its descriptor is closed, symlinks, signal masks, process groups, orphans (init's to wait for; a stopped orphaned group gets `SIGHUP` and `SIGCONT`, the terminal's stops and background reads in one don't stop it), copy-on-write after fork, the `*at()` calls, links, rename, permissions and attributes, paths walked vnode by vnode (`..` after a symbolic link, the cwd and an open directory renamed, `..` through mounts, search permission, a removed cwd, trailing slashes), `SIGPIPE`, `nanosleep`, clocks and `times`; `nanosleep`, `poll` and `select` stopped and continued keep their deadline; signals delivered on the way back to user mode: the SSE state kept across a handler, the handler's signal blocked (`SA_NODEFER`), `sa_mask`, `SA_RESETHAND`, nested handlers, `sigsuspend`, a read stopped and continued made again, a forged `sigreturn` frame |
| `memory`   | with 6 GiB of RAM (2 GiB on i386, which uses 768 MiB), one big mapping (1 GiB, 128 MiB on i386) then 4.5 GiB (512 MiB) of anonymous memory filled, every page with where it is, and read back: memory above 4 GiB, the kernel's small objects (slabs) and its hashmaps for millions of pages |
| `kvmem`    | the kernel's allocator: its self-test at boot (`kvmem.test`: objects of many sizes on pages of their own, their guard pages unmapped, none overwriting another, freed out of order and the arena merged back as it was); forks, execs and threads leave no object behind; a write past an object's end faults at its guard page (`kvmem.guard`) |
| `clock`    | the clock from each of its sources, QEMU's options choosing (`clock=` too): the TSC (invariant), the HPET, the PIT's ticks without an HPET; it never goes back, its resolution (finer than a tick but with the PIT), `nanosleep`, and 2 s as the host counts them (`clock/stamp.py` timestamps the serial lines) |
| `irq`      | interrupts through the APIC and through the 8259 (`apic=off`): `/proc/interrupts` (the controller, the routes), the timer's 500 ticks a second, the NIC's IRQ when a frame comes: the 82540EM's IRQ 11 (level-triggered under the APIC), the 82574L's MSI (its line with the 8259), on QEMU's pc and q35 |
| `net`      | twice, the 82540EM (`e1000`, its line) and the 82574L (`e1000e`, MSI); the e1000 and IPv4: a peer on the host (`net/peer.py`, QEMU's `-netdev socket`) checks the answers to ARP, pings (100 of them, around the rings) and a datagram to a closed port; the guest's program checks process 0 and its network thread (`[net]`, which takes the frames up the stack) in `/proc/0/status`, UDP and ping sockets over loopback (binding, `getsockname`, `FIONREAD`, timeouts, `SO_ERROR` from "port unreachable", `SO_BROADCAST`) and the interfaces' `SIOCGIF*` requests, then talks to the peer: UDP after resolving its address with ARP, a ping, a refused datagram, one through the gateway and a broadcast; TCP over loopback (handshake, data both ways, half-close, reset, `poll`, non-blocking `connect` and `accept`, `SIGPIPE`, a megabyte each way with a child), and with the peer playing TCP segment by segment: a SYN retransmitted, data out of order, fast retransmit and recovery, a retransmission timeout, a zero window probed, a reset, RST for a closed port |
| `tcp`      | TCP against another one: QEMU's user network has its own (from BSD's), the guest (`tcp/init.c`) and `tcp/host.py` exchange megabytes through it: 2 MiB out, an echo server for 4 MiB and 5 connections at once |
| `ext2`     | writes through singly and doubly indirect blocks and appends on an ext2 image, then `e2fsck` and a byte compare on the host |
| `libc`     | errno and signal numbers, stdio, directories, fork/wait, crypt, passwd/shadow and group, truncate, signal masks, the `*at()` calls, `realpath`, `ttyname`, sleeping, `SIGPIPE`, C99 `printf` formats and TCP over loopback (with `stdio` on a socket, `SOCK_NONBLOCK`, `accept4`, `sendmsg`/`recvmsg`), `err(3)`, and the resolver (`/etc/hosts`, `/etc/services`, and a DNS server of the test's own on 127.0.0.1: several addresses, a CNAME, the search domain, a truncated answer and TCP, no such name, PTR) through newlib (needs `make toolchain`) |
| `system`   | the initrd built from source (with contrib/, without ports/): logins on the serial console, then dash runs commands, pipelines, `$(...)`, a long line, `^C`, `/dev/tty` and sbase tools, `nc` over loopback, and the `dhcp` service enabled (sdhcp gets the address from QEMU's user network, a ping goes through) (needs `make toolchain` and `make`) |
| `disk`     | the system installed on a hard disk image (`mk/hdd.sh`): booted from it with Limine twice, a file written on the first boot is read on the second (`mk/hdd-kernel.sh` and `mk/hdd-update.sh` put the kernel and an updated system on it in between), `e2fsck` is clean (needs `make toolchain` and `make`) |
| `tcc`      | the tcc port (`make ports`) on AquilaOS: compiles and runs programs against the C library and libm (skipped without the port) |
| `curl`     | the curl port (`make ports`) on AquilaOS, against HTTPS servers on the host with a CA made for the test: the page fetched over TLS 1.3, a certificate for another name and one from an unknown CA refused (skipped without the port) |

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
