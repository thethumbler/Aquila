# The AquilaOS kernel: code reference {#mainpage}

What each file, function and structure of the kernel is for, from the
comments in its sources. It describes the default build, x86_64; i386
builds from the same files.

For how the kernel works, subsystem by subsystem, start with the kernel's
documents in the repository's `docs/kernel/` (and on aquilaos.com): this
reference is the detail behind them.

- **Files**: the sources, directory by directory (`kernel/arch/i386` is
  the x86 code, both i386 and x86_64; `mm`, `sys`, `fs`, `dev`, `net` the
  rest).
- **Data structures**: the kernel's structures, their fields.

A function's description is its comment, right above it in the source;
the source of each file is a click away.
