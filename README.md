<div align="center">

# esdumanOS

**A 32-bit x86 operating system kernel written from scratch in C and assembly.**

[![CI](https://github.com/iamfurkann/esdumanOS/actions/workflows/ci.yml/badge.svg)](https://github.com/iamfurkann/esdumanOS/actions/workflows/ci.yml)
![Version](https://img.shields.io/badge/version-0.6.1--alpha-blue)
![Architecture](https://img.shields.io/badge/arch-x86__32-orange)
![Language](https://img.shields.io/badge/language-C%20%7C%20x86%20ASM-green)
[![License: MIT](https://img.shields.io/badge/license-MIT-purple)](LICENSE)
![Status](https://img.shields.io/badge/status-alpha-orange)
[![Website](https://img.shields.io/badge/Website-Live-2ea44f)](https://iamfurkann.github.io/esdumanOS-website/)

*An independent operating system, booting through GRUB via Multiboot,*
*with preemptive multitasking, an encrypted file system, and a Unix-style shell.*

</div>

> **⚠️ Alpha Notice:** This is an early development release intended for testing and educational purposes only. It is not suitable for production use. Expect bugs, crashes, and incomplete features.

---

## Table of Contents

- [Overview](#overview)
- [Current Status](#current-status)
- [Features](#features)
- [Architecture](#architecture)
- [Quick Start](#quick-start)
- [Building](#building)
- [Running](#running)
- [Testing](#testing)
- [Project Layout](#project-layout)
- [System Call Reference](#system-call-reference)
- [Security Model](#security-model)
- [Known Limitations](#known-limitations)
- [Roadmap](#roadmap)
- [Contributing](#contributing)
- [License](#license)
- [Credits](#credits)

---

## Overview

esdumanOS is a from-scratch operating system kernel for the x86 (IA-32) architecture. It does not derive from Linux, BSD, or any existing kernel codebase. Every subsystem, from the bootloader handoff through memory management, process scheduling, file system operations, and user authentication, is written specifically for this project.

The kernel boots via GRUB using the Multiboot specification, transitions through Protected Mode with full GDT/IDT/TSS initialization, sets up paged virtual memory, and launches a preemptive multitasking environment with Ring 0 / Ring 3 separation. User-space programs are loaded from ELF binaries, and the system provides a Unix-inspired shell with pipes, output redirection, conditional chaining, and environment variables.

A central design goal is treating security as a first-class concern rather than an afterthought. The kernel includes a tiered security level system, AES-256-CBC disk encryption, user authentication against a shadow password database, and permission enforcement at the system call boundary.

---

## Current Status

**Version:** 0.8.0-alpha

esdumanOS is in the **Alpha** stage. The core kernel subsystems are functional and the
OS boots in QEMU. The privilege boundary is genuinely tested rather than merely
written: the self-test suite hands control to a real Ring 3 process and asserts from
CPL=3, and it also runs under hardware SMEP/SMAP enforcement. Before v0.2.0 every
automated test ran at CPL=0, so process isolation and the scheduler were never
exercised at all.

v0.3.0 moved the working directory into the kernel. Until then the shell kept it in a
userspace global and handed a directory id to every syscall that touched a path, which
meant a process chose where its own relative lookups started — and every `/bin` tool
passed a hardcoded 0, so they all operated on the root directory no matter where you
had `cd`'d to.

v0.4.0 lets a program ask about things rather than only do them: `stat`, `fstat`,
`lseek`, `getpid` and `sleep`. Two of those needed something the tree did not have.
`stat` had to answer with the size a `read()` would return, which is not the size the
directory table records — with encryption on by default, the stored form carries an IV,
a header and padding — so sizes now come from a helper that reads the real length out of
the file's header. And `sleep` is the first production use of `WAIT_TIMER`, which had
been defined since the beginning and referenced only by a test.

v0.4.2 is a stability patch rather than a feature release. The whole v0.4.x tree was
audited before starting work on `fork`/`wait`, and the audit found more than the
roadmap expected: a segfaulting user program left its parent blocked forever and leaked
its entire address space, closing a pipe never woke the process blocked on it, and the
shell overran its own stack on a line of 33 short words. Those and a dozen more are
fixed; the release notes list what is still known-broken rather than quietly carrying
it.

v0.4.3 gives the kernel the ability to write to a file through a descriptor, which it
had never had — `sys_write` handled the console, pipes and `/dev` nodes and dropped
everything else. That single gap was why `/bin/cp` produced empty files and why the
shell's `>` could not be connected to anything. Both work now, within semantics the
disk format dictates: a write is buffered and committed whole when the last descriptor
closes, so there is no appending and no writing into the middle of a file.

v0.4.4 is a header tidy-up ahead of `fork`/`wait`, with no behaviour change. It found
three real defects hiding in declarations: `timer_ticks` was declared without the
`volatile` its definition carries, three declarations in `rtc.h` sat outside the include
guard, and four of the fifteen embedded-ELF lengths were declared with a type that
disagreed with their definitions. The first survived because `timer.c` included none of
the headers that declare what it defines — so nothing ever compared the two.

v0.4.5 splits process teardown apart ahead of `fork`/`wait`. Ending a task and switching
away from it were one function, so the only way to end a task was to *be* it — which is
why `kill` could not kill: a signal to a process with no handler registered was recorded
and then dropped, and every process has no handler registered by default. `SIG_KILL` and
`SIG_TERM` now terminate a target that has not handled them. The same release stops a new
process control block carrying whatever the heap last held, and stops a pid in use from
being issued twice; both are fields `fork()` would have copied.

v0.4.6 touches no kernel code. It makes `make fuzz` explain itself: libFuzzer's runtime
uses the `POPCNT` instruction, and on a CPU without it the target died with `SIGILL`
inside the sanitizer, reported only as "deadly signal" and with a stack trace pointing at
the one place the fault was not. That is reachable whenever the host is an emulated x86 —
QEMU's default CPU model omits the instruction — so it went unseen until development moved
onto Apple Silicon. The same release pins CI to a fixed runner image instead of the
floating `ubuntu-latest`, so a toolchain upgrade is something chosen rather than
discovered.

v0.5.0 adds `fork()` and `wait()`. Until now every process in this system came from an
ELF file: `exec` built an address space and filled it from disk, and the caller blocked
until the result exited. That is enough to run a program and not enough to run two, which
is why the shell executes `cmd1 | cmd2` one stage at a time and deadlocks when the first
stage outgrows the pipe buffer. A process can now be made from a process — the child gets
a private copy of its parent's memory, descriptors, signal handlers and registers, and
returns from the call as if it had made it itself. The shell does not use any of this
yet; that is the next release, and it is deliberately separate so that `fork` is verified
before anything is built on it.

v0.5.1 changes no kernel code at all. Test and production builds compiled the same
sources with different flags into the same objects, and make cannot see a flag change —
so every test run began by deleting every object and paying for a full rebuild, and a
release image built without `make clean` first would quietly inherit the reduced PBKDF2
iteration count. Each flavour now has its own tree under `build/`, which removes the
hazard by construction and makes incremental builds work again: a one-file change takes
seconds instead of two minutes.

v0.5.2 puts the shell on `fork`. Both stages of `cmd1 | cmd2` now run at once, so a first
stage producing more than the 4 KB the pipe holds no longer blocks with no reader and
takes the shell down with it. A trailing `&` runs a command in the background, `jobs`
lists what is still running and `wait` blocks for all of it — which together mean there
is finally a way to hold a prompt while another process runs, and therefore a way to try
`kill` by hand. That absence is why `kill` went five releases without anyone noticing it
did nothing.

v0.5.3 finishes the pipeline by giving both of its ends a way to stop. A writer whose
reader has gone is now sent `SIGPIPE` and dies; refusing the write, which is all v0.5.2
did, was only a return value, and nothing in user space reads one — so the stage ran to
the end of its input with every write failing in silence. At the other end, `grep`, `head`
and the new `wc` read standard input when no file is named, which is what makes a pipeline
have somewhere to go: until now `cat` was the only program in the system that would take
one. Reading standard input from a terminal needed an end of its own, so Ctrl-D became a
real key — the console could not report end-of-file at all, and on a machine with one
terminal and no Ctrl-C, a read that cannot end takes the machine with it. Testing all of
that turned up one more thing: `ls` and `dmesg` printed from inside the kernel, straight
to the screen, so they had never gone through a pipe or a redirect in their lives.
Both now hand their output back and let the shell write it.

v0.5.4 stops the parser guessing. `a | b | c` used to run `a` against the literal tokens
`b | c`, and `a | b > f` handed `> f` to `b` as two words: the parser took whichever
operator it met first and passed the rest on as ordinary arguments, so the wrong thing ran
and reported success. A pipeline is up to four processes now, `>` belongs to the stage it
appears in, and the cases that are still not supported are refused with a message instead
of reinterpreted. Three programs in `/bin` — `rm`, `mv` and `kill` — became reachable in
the same change, having shipped in every image so far and been shadowed by builtins of the
same name. The other half of the release is that `/etc` finally holds something: the
hostname the prompt prints, the version the kernel was built with, a message of the day,
and a `profile` the shell reads before its first prompt.

v0.6.0 makes the clock something a program can read, and something a user can configure.
The offset was compiled in, so a machine in the wrong place had to rebuild the kernel;
`/etc/timezone` holds it now — a signed hour count rather than a zone name, because there
is no timezone database here and there will not be one on a 2 MB disk. `date -u` prints
UTC, with the shift done in the kernel so the calendar carry lives in one place. The clock
was reachable from Ring 0 only —
it drew the status bar and nothing else — so `date` printed a string compiled into its own
binary, the same one on every boot. `TIME` (55) hands the broken-down fields to user space,
and `date` uses them. Two defects went with it: the timezone offset was applied with a day
carry that never looked at how long the month was, so 21:00 UTC on 31 August produced
`32/08`; and the RTC's update-in-progress flag was checked once before seven registers
were read one after another, which lets a reading straddle an update and, at midnight,
produce a date that never existed.

v0.6.1 makes the log a log. It was a fill-once buffer that stopped accepting at 8 KB and
silently dropped every record after, while this file and its own source both called it a
ring buffer — so `dmesg` showed the oldest part of the boot and nothing since. What filled
it was `printk()`, which fed every character the kernel printed into it: the boot banner,
the ASCII art and the first-boot password prompts all competed for the space with actual
records. It wraps now, and holds records rather than a transcript of the screen.
`/var/log` also stops being an empty directory — the log is written to `kern.log` at
`sync`, `halt` and `reboot`.

v0.7.0 stops `fork()` from copying. A child used to receive a private duplicate of every
page its parent had mapped, made at the moment of the call — a 32-page user stack before
anything else — and the overwhelmingly common next call is `exec()`, which discards all of
it. The child now gets the parent's frames themselves, both sides give up write access,
and the first write from either of them splits the page it touched. What made the copy
necessary was the teardown path, which released every user frame it found unconditionally;
the physical allocator counts owners now, so two address spaces can hold a page and let go
of it independently. Three memory defects went with it, all of them latent: the kernel heap
advanced its end pointer over a mapping that had failed, so the next block header was
written into an unmapped hole; the ELF loader lost a frame on every failed `exec()`; and
`kfree()` merged blocks that were neighbours in its list without checking they were
neighbours in memory. Two paths would have broken silently under sharing and were found by
reading rather than by testing — the writable-pointer validator rejects a page whose
read/write bit is clear, which after a fork is every writable page on both sides, and the
page fault handler examined the in-progress-copy fixup before it examined the page.

v0.7.1 gives user-space programs memory they can ask for. Until now a program had
its ELF segments and a fixed 32-page stack, decided once by the loader, and every tool
in `/bin` worked from arrays sized at compile time - there was no allocator, and with
each program compiled as a single translation unit against `-nostdlib`, nowhere to put
one. Two syscalls now: `brk` moves a single boundary upwards from the end of the image,
and `mmap` hands out an independent run below the stack that `munmap` releases on its
own. `include/umalloc.h` is the allocator over both, in a header because there is no
link step that would find a library; small requests come off the break, and anything
from 64 KB up takes its own mapping so that freeing it really returns the pages. Both
kinds arrive zeroed, which is a security property rather than a courtesy - a frame the
allocator has just handed out holds whatever its last owner left in it. `ls` is the
first user: its listing buffer moved off the stack and grew to the 4 KB the kernel was
always willing to return, four times what it had been reading.

v0.7.2 makes a log record a record. v0.6.1 made the ring wrap and stopped `printk()`
feeding it, so it stopped being a transcript of the screen — but a record was still
just a line of text, and every question about one was a question about parsing.
When did this happen, how many did we lose when it wrapped, show me only the
errors: none could be answered, so none were asked. The ring holds structured
records now, 512 of them against the 8 KB of flat text that held roughly 130
lines, and each carries its own level, module, monotonic timestamp and sequence
number. The sequence numbers are what make a gap visible: `dmesg` reports how many
records went when the ring wrapped, which nothing could ask before. `KLOG_CTL`
gives user space the rest — the severity threshold had sat at INFO since boot with
nothing able to move it, so every DEBUG record the kernel composed was discarded
unseen. `dmesg` grew `-c`, `-n` and `-l` to reach it, and `/dev/kmsg` streams the
records in a machine-readable form, writable by root so that a program's words are
recorded as a program's rather than as the kernel's.

It remains an early development release, intended for developers, OS enthusiasts, and
anyone curious about kernel internals — not for storing anything you care about.

**What works:**
- Boots via GRUB, initializes all subsystems, and launches a Unix-style shell
- Preemptive multitasking with ELF binary execution
- Encrypted file system with AES-256-CBC
- User authentication and security levels
- 16 user-space programs and 28 shell builtins
- 31 kernel self-test modules and CI pipeline

**What to expect:**
- This is not production-ready software
- You may encounter kernel panics, deadlocks, or unexpected behavior
- Resource limits are intentionally constrained (16 processes, 2 MB disk, 128 MB RAM)
- No networking, no GUI, no dynamic linking

---

## Features

### Kernel Core

| Component | Description |
|-----------|-------------|
| **Boot** | Multiboot-compliant entry, 16 KB kernel stack, identity-mapped first 16 MB |
| **GDT / IDT / TSS** | 9-entry GDT with Ring 0 and Ring 3 segments, 256-vector IDT with PIC remapping, one TSS for privilege transitions and a second for the double-fault task gate |
| **Syscall Interface** | 55 system calls via INT 0x80, covering process control, file I/O, IPC, security, and device access |
| **Terminal (ANSI)** | Cursor positioning and relative motion, erase display and line, colour and attributes, saved cursor, scroll region, line insert and delete. Rows are counted in the 24 the screen shows; row 0 is the status bar |
| **Kernel Logging** | 512-record ring; each record carries its own level, module, monotonic timestamp and sequence number. Readable through the `dmesg` syscall and `/dev/kmsg`, controlled through `KLOG_CTL`, written to `/var/log/kern.log` at `sync`, `halt` and `reboot`. Records only — the screen transcript is not part of it |
| **Spinlocks** | Interrupt-safe kernel spinlock primitives |

### Memory Management

| Component | Description |
|-----------|-------------|
| **Physical Memory (PMM)** | Bitmap allocator with per-frame reference counting, multiboot memory map detection, 128 MB addressable, next-fit optimization |
| **Virtual Memory (Paging)** | Recursive page directory mapping, per-process address spaces, 4 KB page granularity, copy-on-write on `fork` |
| **Kernel Heap** | First-fit allocator with block splitting, bidirectional coalescing with adjacency checks, magic-number corruption detection, double-free protection |
| **User Memory** | Program break (`brk`) and anonymous `mmap`/`munmap`, both zero-filled; `include/umalloc.h` is a header-only allocator over them |

### Process Management

| Component | Description |
|-----------|-------------|
| **Scheduler** | Preemptive, priority-based, with kernel-mode preemption guard |
| **ELF Loader** | Loads PT_LOAD segments, sets up user stack with guard page, inherits file descriptors |
| **Working directory** | Per-process, held in the PCB and inherited from the creating process. Relative paths resolve against it; user space can only move it through `chdir()` |
| **IPC** | Message passing (8-slot mailbox per process), anonymous and named pipes (16 pipes, 4 KB ring buffer each) |
| **Signals** | Per-process signal handlers (32 slots), kernel timer slots (32) |
| **FPU** | Eager FPU state save/restore on every context switch (FXSAVE/FXRSTOR), per-process 512-byte state. Not lazy — there is no `CR0.TS` / `#NM` path |

### File System

| Component | Description |
|-----------|-------------|
| **VFS** | Custom FAT-based file system with flat directory table, CRUD operations, atomic file updates |
| **CryptoFS** | Transparent AES-256-CBC encryption layer. Per-file IVs are derived as `HMAC-SHA256(file key, label ‖ counter ‖ pool bytes)`, so they stay distinct even when the entropy pool has nothing to offer; HMAC-SHA256 over the plaintext for integrity |
| **Block Cache** | 64-slot LRU sector cache, write-back. Dirty sectors are written out when 32 slots are outstanding, when any has waited 5 seconds, or on explicit `sync()` |
| **DevFS** | `/dev/null`, `/dev/random`, `/dev/urandom` and `/dev/kmsg` device nodes; the random devices are ChaCha20 keyed from the kernel entropy pool and re-keyed periodically, and `/dev/kmsg` streams log records with a per-process cursor |

### Drivers

| Driver | Description |
|--------|-------------|
| **ATA/IDE** | PIO-mode disk I/O with IRQ-based waiting, 28-bit LBA, single-sector read/write, cache flush |
| **PS/2 Keyboard** | IRQ1 handler, US and Turkish layouts, Shift/CapsLock/AltGr, 256-byte ring buffer |
| **VGA Text** | 3 virtual terminals (F1-F3 switching), 80x100 scrollback buffer, status bar, cursor management |
| **RTC** | CMOS real-time clock, BCD/binary auto-detection, 12/24-hour conversion |

### Cryptography

| Algorithm | Description |
|-----------|-------------|
| **AES-256** | Full implementation supporting ECB, CBC, and CTR modes (based on tiny-AES-c) |
| **SHA-256** | Incremental implementation (init/update/final), verified against NIST vectors |
| **HMAC-SHA256** | RFC 2104, verified against RFC 4231 test vectors. Used for file integrity and IV derivation |
| **PBKDF2-HMAC-SHA256** | Password key derivation, verified against RFC 6070 vectors. Iteration count is clamped to a sane range on read |
| **ChaCha20** | Stream cipher backing `/dev/random` and `/dev/urandom`, keyed from the entropy pool |
| **Entropy pool** | RDRAND when available; otherwise interrupt timing jitter with per-source entropy budgets and an honest quality verdict |

### User Space

| Component | Description |
|-----------|-------------|
| **Shell** | Login screen, 28 builtins (cat, ls, cd, pwd, mkdir, rm, mv, write, env, export, exec, kill, su, sleep, dmesg, hexdump, help and more), two-stage pipe operator, output redirection, `&&`/`\|\|` chaining, `$VAR`/`$?`/`~` expansion |
| **Programs** | 16 standalone ELF binaries: `sh`, `hello`, `echo`, `clear`, `touch`, `rm`, `mv`, `cp`, `free`, `whoami`, `kill`, `grep`, `head`, `wc`, `date`, `stat` |
| **FHS Layout** | `/bin`, `/dev`, `/etc`, `/home`, `/root`, `/tmp`, `/var` created at boot |
| **Authentication** | Password-protected login, `/etc/shadow` database, `su` for user switching |

### Testing and CI

| Layer | Description |
|-------|-------------|
| **Kernel Self-Tests** | 23 kernel-mode modules: VFS, memory, pipe, security, passwd, devfs, regression, integration, adversarial, concurrency, stress, string, paging, PMM, lifecycle, fault, syscall, process, signal, ELF, crypto, entropy, bcache — plus a Ring 3 payload that exercises the privilege boundary from the unprivileged side |
| **Host Tests** | Crypto verification, ELF static analysis, ELF validation, hash validation |
| **Fuzzing** | Parser fuzz testing with 54 corpus files |
| **CI Pipeline** | GitHub Actions: host tests, fuzzing, OS build, QEMU kernel integration tests |

---

## Architecture

```
                     +--------------------------------------------------+
                     |                   User Space                     |
                     |                                                  |
                     |    init (shell)     hello     echo     clear     |
                     |         |             |        |         |       |
                     +---------|-------------|--------|---------|-------+
                               |             |        |         |
                          INT 0x80      INT 0x80  INT 0x80  INT 0x80
                               |             |        |         |
    +--------------------------|-------------|--------|---------|-------+
    |                    System Call Dispatcher (50 syscalls)           |
    +------------------------------------------------------------------+
    |                                                                   |
    |   +-------------+  +-------------+  +-------------+  +---------+ |
    |   |   Process    |  |    File     |  |   Memory    |  |  Security| |
    |   |  Management  |  |   System    |  | Management  |  |  Module  | |
    |   |             |  |             |  |             |  |          | |
    |   | - Scheduler |  | - VFS (FAT) |  | - PMM       |  | - Auth   | |
    |   | - ELF Load  |  | - CryptoFS  |  | - Paging    |  | - Levels | |
    |   | - Pipes/IPC |  | - bcache    |  | - Heap      |  | - Passwd | |
    |   | - Signals   |  | - DevFS     |  |             |  | - KDF    | |
    |   +-------------+  +-------------+  +-------------+  +---------+ |
    |                                                                   |
    |   +-----------------------------------------------------------+   |
    |   |                     Hardware Abstraction                  |   |
    |   |   ATA Disk  |  PS/2 Keyboard  |  VGA Text  |  RTC Clock  |   |
    |   +-----------------------------------------------------------+   |
    |                                                                   |
    |   +-----------------------------------------------------------+   |
    |   |                   x86 CPU Infrastructure                  |   |
    |   |   GDT (9 entries)  |  IDT (256 vectors)  |  TSS  |  PIC  |   |
    |   +-----------------------------------------------------------+   |
    +-------------------------------------------------------------------+
                               |
                         Multiboot / GRUB
                               |
                          Physical Hardware
```

### Virtual Memory Map

```
0xFFFFFFFF  +-------------------------+
            |  Recursive Page Dir     |  0xFFFFF000 - PD maps itself
0xC0000000  +-------------------------+
            |  Kernel Space           |  Identity-mapped first 16 MB
            |  (kernel code, data,    |
            |   heap, page tables)    |
0xBFFFFFFF  +-------------------------+
            |  (unmapped)             |  Top of the user range; validated as
            |                         |  writable but nothing is mapped here
0xB0000000  +-------------------------+  USER_STACK_TOP
            |  User Stack             |  32 pages, grows down
0xAFFDF000  +-------------------------+  Guard page, never mapped
            |  mmap region            |  Anonymous mappings, allocated
            |                         |  downwards from the guard page
0xA0000000  +-------------------------+  USER_MMAP_FLOOR
            |                         |
            |  (unmapped)             |  The two regions grow towards each
            |                         |  other and stop at this fixed split
            |  Program break / heap   |  Grows up from the end of the image
            |                         |
            |  User Space             |  Process-specific mappings
            |  (ELF segments)         |
0x00400000  +-------------------------+  User space lower bound
            |  Kernel Low Memory      |  First 4 MB (supervisor only)
0x00100000  +-------------------------+  Kernel load address (1 MB)
            |  Real Mode / BIOS       |
0x00000000  +-------------------------+
```

### Boot Sequence

```
GRUB (Multiboot)
  |
  v
boot.asm -----> Set up page tables (identity + higher-half)
  |              Enable paging (CR0 bit 31)
  |              Set up 16 KB kernel stack
  v
kernel_main()
  |
  +---> init_terminal()          VGA text mode, 3 virtual terminals
  +---> init_gdt()               9-entry GDT, Ring 0 + Ring 3 segments, #DF TSS
  +---> init_idt()               256 IDT entries, PIC remapping
  +---> init_security()          Seeds the entropy pool (RTC + TSC)
  +---> init_elf_master_key()    Loads the build-time ELF decryption key
  +---> init_pmm()               Bitmap allocator from multiboot memory map
  +---> init_paging()            Recursive page directory, identity map 16 MB
  +---> init_kernel_heap()       First-fit heap allocator
  +---> init_timer(TIMER_HZ)     PIT at 100 Hz
  +---> init_signals()           Kernel timer slot table
  +---> ata_identify()           ATA disk detection
  +---> fs_init()                VFS, FAT, directory table from disk
  +---> init_fpu()               FPU/SSE detection and initialization
  +---> init_multitasking()      Idle task, task array
  +---> Create FHS hierarchy     /bin, /dev, /etc, /home, /root, /tmp, /var
  +---> First boot setup         Prompts for the root and user passwords,
  |                              then writes /etc/passwd and /etc/shadow
  +---> Load ELF programs        Decrypt and write init and the /bin tools
  +---> load_and_exec_elf()      Load init into its own address space
  v
start_first_task() ------------> iret to Ring 3, init shell starts
```

---

## Quick Start

```bash
# Clone the repository
git clone https://github.com/iamfurkann/esdumanOS.git
cd esdumanOS

# Build and run in QEMU (requires toolchain, see Building section)
make run
```

The kernel boots inside your terminal (`make run` passes `-display curses`, not a
separate window). There is no default password: on a fresh
disk the first boot runs a setup prompt that asks you to choose passwords for
`root` and for the `esduman` user, and only then writes `/etc/shadow`. Subsequent
boots go straight to the login prompt.

The exception is a self-test build (`make test_kernel`), which needs to run
unattended and so creates both accounts with the password `test`.

---

## Building

### Requirements

| Tool | Minimum Version | Purpose |
|------|----------------|---------|
| `gcc` | 9.0+ | Cross-compilation (C kernel code) |
| `nasm` | 2.14+ | x86 assembly |
| `ld` | GNU ld 2.30+ | Linking |
| `make` | GNU Make 4.0+ | Build system |
| `grub-mkrescue` | 2.04+ | Bootable ISO creation |
| `xorriso` | 1.5+ | ISO 9660 filesystem (used by grub-mkrescue) |
| `mtools` | 4.0+ | FAT filesystem utilities (used by grub-mkrescue) |
| `qemu-system-i386` | 5.0+ | x86 emulation |
| `openssl` | 1.1+ | Build-time ELF encryption tooling |
| `python3` | 3.6+ | Test and tooling scripts |

The host may be 64-bit; `gcc-multilib` is what lets a 64-bit compiler emit the
32-bit objects this kernel is built from, and it is what CI uses. A 32-bit host
works too but is not required by anything here.

> **Developing inside an emulated x86 VM?** The host CPU must implement `POPCNT`.
> QEMU's default `qemu64` CPU model does not, and libFuzzer's runtime uses that
> instruction — so `make fuzz` dies with `SIGILL` inside the sanitizer before any
> of this project's code runs, reported only as "deadly signal". Start the VM with
> `-cpu max` (or any model from Nehalem onward) and confirm with
> `grep -c popcnt /proc/cpuinfo`. This bites on Apple Silicon, where an x86 guest
> is always emulated; real hardware and the CI runners are unaffected. `make fuzz`
> now checks for it and says so rather than failing this way.

### Installing Dependencies

**Ubuntu / Debian:**

```bash
sudo apt-get update
sudo apt-get install -y gcc-multilib nasm make qemu-system-x86 \
    grub-common grub-pc-bin xorriso mtools libssl-dev python3
```

A minimal Debian netinst does not ship everything the build needs. Add
`make`, `git`, `xxd` (the ELF embedding step calls `xxd -i`), and `bear` if you
want a `compile_commands.json` for your editor.

**Fedora:**

```bash
sudo dnf install gcc nasm make qemu-system-x86 \
    grub2-tools-extra xorriso mtools openssl-devel python3
```

**Arch Linux:**

```bash
sudo pacman -S gcc nasm make qemu-system-x86 \
    grub xorriso mtools openssl python
```

### Build Commands

> **Required:** `ESDUMAN_ELF_KEY_HEX` must be set, and it must be **exactly 64
> hexadecimal characters** — the 32-byte AES-256 key used to encrypt the embedded
> ELF binaries. The Makefile checks both and aborts otherwise. A passphrase-style
> value will not work.
>
> ```bash
> export ESDUMAN_ELF_KEY_HEX=$(openssl rand -hex 32)
> ```
>
> The key ends up compiled into the kernel image, so treat it as a build
> parameter rather than a secret — see [Known Limitations](#security).

```bash
# Full build: compile kernel, encrypt ELF binaries, create bootable ISO
make

# Clean all build artifacts
make clean
```

Objects go into `build/<flavour>/`, mirroring the source tree — `build/prod` for the
release image, `build/test` for the self-test binary, `build/dev` for `make run-dev`.
The three differ by compiler flags that make cannot see, so they are kept apart by
directory instead: `make` and `make test_kernel` can be run in any order without a
`clean` between them, and an incremental build recompiles only what changed.

> `make ARCH=riscv64` stops with a diagnostic: the Makefile carries a RISC-V
> branch, but `arch/riscv/` is not present in this tree. Only `ARCH=x86` builds.

The build process:
1. Compiles all C and assembly source files with `-m32 -nostdlib -nodefaultlibs
   -fno-builtin` (user-space programs additionally get `-ffreestanding`)
2. Builds the 15 user-space ELF programs plus `init`
3. Encrypts each ELF with AES-256-CBC using `ESDUMAN_ELF_KEY_HEX` and embeds the
   ciphertext as a C array via `xxd -i`
4. Links the kernel binary against the custom linker script (load at 1 MB,
   higher-half at 0xC0000000)
5. Creates a GRUB-bootable ISO image via `grub-mkrescue`

---

## Running

### QEMU (Recommended)

```bash
# Build and boot in one step
make run
```

This executes QEMU as:

```
qemu-system-i386 -cdrom esdumanOS-v0.5.2-alpha.iso -serial file:kernel_log.txt \
    -drive format=raw,file=disk.img,if=ide,index=0,media=disk -display curses
```

Which means:
- **`-display curses`** — the OS runs inside your terminal, not a separate window.
  Quit with `Esc` then `2` to reach the QEMU monitor, or `Ctrl-A X` under `-nographic`.
- **Serial output goes to `kernel_log.txt`**, not to the terminal. That file is
  where `klog` output lands; tail it in another shell while the OS runs.
- Bootable CD-ROM from the generated ISO, plus a 2 MB raw disk image on the
  primary IDE channel.
- No `-m` flag is passed, so QEMU's default for i386 applies — 128 MB, which is
  what the PMM reports at boot.
- No debug-exit device: that is added only by `make test_kernel` and `make test_smap`.

### Manual QEMU Invocation

To get a graphical window and the serial log on your terminal instead of the
defaults above:

```bash
qemu-system-i386 \
    -m 128 \
    -cdrom esdumanOS-v0.5.2-alpha.iso \
    -drive file=disk.img,format=raw,if=ide \
    -serial stdio
```

Add `-device isa-debug-exit,iobase=0xf4,iosize=0x04` only if you want the kernel
to be able to terminate QEMU with an exit code, as the self-test targets do.

The ISO filename carries the version, and the Makefile derives it from the
`OS_VERSION_*` macros in `include/kernel.h` — so it changes on a version bump and
there is no second place to update. Run `make -pn | grep '^ISO ='` if you are not
sure what the current build produces.

### Keyboard Shortcuts (Inside the OS)

| Key | Action |
|-----|--------|
| F1 / F2 / F3 | Switch between virtual terminals |
| Up / Down Arrow | Scroll terminal history |
| AltGr | Access Turkish keyboard layout characters |
| Ctrl-D | End input for a program reading the keyboard (`cat`, `grep`, `head`, `wc`) |

Ctrl with a letter produces that letter's control code, which is how Ctrl-D becomes the
end-of-file byte. Ctrl with anything else is passed through unchanged. There is no
Ctrl-C: interrupting a foreground process needs process groups, which do not exist yet.

### Shell Commands

Once logged in, the following builtins are available:

```
cat [-nbEsTA] <file>  Print file contents
ls                    List the working directory (takes no argument)
cd [dir]              Change directory; supports ., .., ~ and -
pwd                   Print working directory
mkdir <name>          Create directory
rm <file>             Remove file
mv <old> <new>        Rename a file within one directory
write <file> <text>   Create a file with the given contents
cat_raw <file>        Hex dump a file's stored bytes, bypassing decryption
env                   List environment variables
export KEY VALUE      Set environment variable (two words, not KEY=VALUE)
sleep <seconds>       Pause for a number of seconds
exec <program>        Execute an ELF binary
kill <pid> <signal>   Send a signal to a process (decimal). 9 and 15 terminate a
                      target that has not registered a handler for them.
su                    Switch to root (prompts for the root password)
dmesg                 Display the kernel log
meminfo               Display memory usage (root only)
hexdump <addr>        Hex dump memory (root only)
stack                 Dump the current task's stack (root only)
layout tr|us          Set the keyboard layout
lockdown              Enter the lockdown security level
help                  Show available commands
reboot                Reboot the system
halt                  Halt the CPU
exit                  Exit the shell
```

`echo` and `clear` are not builtins — they are ELF programs in `/bin`, reached
through the same path as any other program. `echo` does not implement `-n`.

**Operators:** Pipes (`cmd1 | cmd2 | cmd3`, up to four stages), output redirection
(`cmd > file`), chaining (`cmd1 && cmd2`, `cmd1 || cmd2`).

Four stages is a process budget rather than a preference: an external stage costs two
tasks, because the forked child runs the program through `exec()`, which creates a task of
its own. A fifth is refused with a message, as are a bare `|` and a backgrounded pipeline.

A command word containing a `/` is a path and is never matched against the builtin table,
so `rm` is the builtin and `/bin/rm` is the program of that name.

**Reading standard input.** `cat`, `grep`, `head` and `wc` read standard input when no
file is named, which is what lets them sit at the far end of a pipe:

```
echo hello | grep hello
cat /etc/passwd | wc
ls /bin | grep wc
dmesg | head
```

Run bare, they read the keyboard instead, and **Ctrl-D** ends that input. A program
writing into a pipe whose reader has finished is sent `SIGPIPE` and terminates, so
`something | head` stops the producer once `head` has its ten lines rather than letting it
run to the end of its input. The shell declines `SIGPIPE` for itself — losing it would end
the session — and restores the default in every process it forks.

Redirection truncates: the target is created if it does not exist and emptied if it does.
It can be combined with a pipe, and belongs to the stage it appears in — in `a | b > f`,
`b`'s output goes to the file.

**Writing a file is all-or-nothing.** Bytes written through a descriptor are held
until the last descriptor closes and then committed as the file's entire new
contents, so there is no appending (`>>`), no writing into the middle of a file,
and no seeking during a write. That is not a shortcut: a stored file is a single
AES-CBC blob authenticated over its whole plaintext, so it can be replaced but
never extended. A single file is capped at 64 KB on this path.

**Variables:** `$VAR` expansion, `$?` last exit code, `~` home directory. Expansion runs
over the whole line before it is split into stages, so a variable in any stage is
expanded, and each `~` gets storage of its own.

**Startup files.** The shell reads three files under `/etc` before its first prompt, and
carries on without any of them. `/etc/hostname` supplies the name in the prompt,
`/etc/motd` is printed, and `/etc/profile` is applied — only `export KEY VALUE` lines,
because it is a settings file rather than a script. `/etc/timezone` is read by the kernel
rather than the shell, and holds a signed hour offset rather than a zone name.

---

## Testing

esdumanOS includes a multi-layered test infrastructure:

```bash
# Run host-side unit tests (crypto, ELF analysis, hash)
make test

# Run parser fuzzing with 54 corpus files
make fuzz

# Boot kernel in self-test mode: 31 kernel-mode modules, then a Ring 3 payload
make test_kernel

# Run one module instead of all of them, for iteration
make test_kernel MODULE=fork

# Run only the Ring 3 payload
make test_kernel MODULE=ring3

# Same suite on a CPU that exposes RDRAND, to cover the strong-entropy path
make test_kernel QEMU_TEST_CPU="-cpu qemu32,+rdrand"

# Same suite with SMEP/SMAP enforced. The kernel-mode modules are skipped there
# (they stand in for user space from Ring 0, which SMAP forbids); the Ring 3
# payload covers the boundary from the correct side.
make test_smap
```

`MODULE=` names one of the modules in the table at the top of
`tests/kernel/selftest.c`, or `ring3` for the user-mode payload alone. It exists
because a full run boots the OS and executes everything, which costs minutes on an
emulated host whether or not the change under test touches any of it. An unknown
name lists what is available and fails the run rather than executing nothing and
reporting a pass. **CI always runs the full suite**, and so must a release: a
filtered run proves one module, not the tree.

### Kernel Test Modules

| Module | Coverage |
|--------|----------|
| `test_vfs.c` | File create/delete, directory nesting (15 levels), path resolution |
| `test_memory.c` | Heap allocation, deallocation, read/write verification |
| `test_pipe.c` | Pipe creation, ring buffer, EOF detection, syscall integration |
| `test_security.c` | Authentication: wrong password, invalid UID, correct password |
| `test_passwd.c` | `/etc/passwd` protection: delete/overwrite/rename rejection |
| `test_adversarial.c` | Pointer validation: NULL, kernel space, upper bounds |
| `test_stress.c` | FD exhaustion (16 limit), long filename handling |
| `test_regression.c` | 5 previously-fixed bugs: kfree(NULL), PID confusion, ATA limits |
| `test_integration.c` | Cross-component: VFS lifecycle, ELF load-to-process |
| `test_concurrency.c` | Hardware atomic operations (`__sync_lock_test_and_set`) |
| `test_devfs.c` | Device filesystem: `/dev` nodes, invalid device rejection, DRBG re-keying |
| `test_string.c` | libft string function correctness |
| `test_paging.c` | Map/unmap, collision rejection, CR0.WP enforcement on read-only user pages |
| `test_pmm.c` | Frame allocation, free-memory accounting |
| `test_lifecycle.c` | Address space clone and teardown; asserts every frame is reclaimed |
| `test_fault.c` | Double-fault infrastructure: task gate, TSS descriptor, dedicated stack |
| `test_syscall.c` | Dispatcher rejection of bad numbers, FDs, and sizes |
| `test_process.c` | Scheduler, live-frame detection, rwlocks, syscall restart, idle task |
| `test_signal.c` | Handler registration and pending-signal bookkeeping |
| `test_elf.c` | Loader validation: bad sizes, overflowing offsets, kernel load addresses |
| `test_crypto.c` | SHA-256 / HMAC / PBKDF2 against published vectors; CryptoFS round trip |
| `test_entropy.c` | Extraction uniqueness, per-source entropy budgets, IV and salt distinctness |
| `test_bcache.c` | Cache hits, and the write-back policy: volume bound, time bound, `sync()` |
| `test_time.c` | Calendar carry both ways, leap years including the century rule, the `TIME` syscall |

The Ring 3 payload (`tests/user/ktest_user.c`) runs after these and reports its
results back through a dedicated syscall. It is the only part of the suite that
actually crosses the privilege boundary, covering user-pointer validation, pipes,
`readdir`, process teardown, and lockdown behaviour from an unprivileged process.

---

## Project Layout

```
esdumanOS/
|
|-- arch/
|   +-- x86/
|       |-- boot/
|       |   +-- boot.asm             Multiboot entry, page table setup, stack init
|       |-- cpu/
|       |   |-- gdt.c                Global Descriptor Table (9 entries)
|       |   |-- idt.c                Interrupt Descriptor Table, PIC remapping
|       |   |-- isr.c                Interrupt dispatcher, exception handlers
|       |   |-- timer.c              PIT configuration (TIMER_HZ = 100 Hz)
|       |   +-- tss.c                Task State Segment, double-fault TSS
|       +-- linker.ld                Kernel linker script (load at 1 MB)
|
|-- kernel/
|   |-- core/
|   |   |-- kernel.c                 Entry point, subsystem initialization
|   |   |-- klog.c                   Ring buffer kernel logger
|   |   +-- uaccess.c                Validated copies across the user boundary
|   |-- proc/
|   |   |-- process.c                Scheduler, IPC, mutexes, context switch
|   |   |-- elf.c                    ELF binary loader
|   |   |-- elf_validate.c           Header and segment validation (also fuzzed)
|   |   |-- pipe.c                   Anonymous and named pipes
|   |   +-- signal.c                 Timer-based kernel timers
|   |-- syscall/
|   |   |-- syscall.c                Dispatcher, 55 system calls
|   |   +-- sys_*.c                  Handlers by area: fs, ipc, process, sec, utils
|   +-- security/
|       |-- security.c               Security levels, master key lifetime
|       |-- entropy.c                Entropy pool, RDRAND and interrupt jitter
|       +-- passwd.c                 User authentication, shadow database
|
|-- mm/
|   |-- pmm.c                        Physical memory manager (bitmap)
|   |-- paging.c                     Virtual memory, page directory cloning
|   |-- paging_s.asm                 CR3/CR0 helpers
|   +-- kheap.c                      Kernel heap allocator
|
|-- fs/
|   |-- vfs.c                        Virtual file system, FAT, directory table
|   |-- crypto_fs.c                  AES-256-CBC transparent encryption
|   |-- bcache.c                     Block cache (64-slot LRU, write-back)
|   +-- devfs.c                      Device filesystem (/dev/null, /dev/random, /dev/urandom)
|
|-- drivers/
|   |-- ata.c                        ATA/IDE PIO disk driver
|   |-- keyboard.c                   PS/2 keyboard (US + Turkish)
|   |-- tty.c                        VGA text mode, 3 virtual terminals
|   +-- rtc.c                        Real-time clock
|
|-- crypto/
|   |-- aes.c                        AES-256 (ECB / CBC / CTR)
|   |-- sha256.c                     SHA-256, incremental and one-shot
|   |-- hmac.c                       HMAC-SHA256
|   |-- pbkdf2.c                     PBKDF2-HMAC-SHA256
|   +-- chacha20.c                   ChaCha20 stream cipher
|
|-- lib/                             Freestanding standard library (48 files)
|   |-- stdio.c                      kvsnprintf, printk
|   +-- ft_*.c                       String, memory, character, list utilities
|
|-- apps/
|   |-- init.c                       User-space shell and login
|   +-- bin/
|       |-- sh.c                     Standalone shell
|       |-- hello.c                  Hello World ELF program
|       |-- echo.c                   Echo command
|       |-- clear.c                  Clear screen
|       |-- touch.c                  Create empty file
|       |-- rm.c                     Remove file
|       |-- mv.c                     Move/rename file
|       |-- cp.c                     Copy file
|       |-- free.c                   Display memory info
|       |-- whoami.c                 Print current user
|       |-- kill.c                   Send signal to process
|       |-- grep.c                   Search text patterns (file or stdin)
|       |-- head.c                   Display first lines of file or stdin
|       |-- wc.c                     Count lines, words and bytes
|       |-- date.c                   Display date and time
|       +-- stat.c                   Show a file's size, type and owner
|
|-- include/                         41 header files
|   |-- kernel.h                     Master header (version 0.6.1-alpha)
|   |-- types.h                      Integer type definitions
|   |-- syscall.h                    50 syscall number definitions
|   |-- process.h                    Process control block, scheduler API
|   |-- fs.h                         VFS structures, file operations
|   |-- stat.h                       esd_stat_t and the lseek origins
|   |-- paging.h                     Virtual memory constants
|   |-- entropy.h                    Entropy pool API and quality contract
|   +-- security.h                   Security level enumeration
|
|-- tests/
|   |-- kernel/                      31 kernel-mode test modules + framework
|   |-- user/                        Ring 3 test payload
|   +-- host/                        Host-side tests, fuzzing (54 corpus files)
|
|-- tools/                           Build-time utilities
|   |-- mkfs.py                      File system image creator
|   +-- encrypt_tool.c               ELF encryption tool (links OpenSSL libcrypto)
|
|-- grub/
|   +-- grub.cfg                     GRUB bootloader configuration
|
+-- Makefile                          Build system. Only ARCH=x86 is present; the
                                      ARCH=riscv64 branch is scaffolding and
                                      stops with a diagnostic.
```

---

## System Call Reference

The kernel exposes 55 system calls through `INT 0x80`. The syscall number is passed in `EAX`.

### Process Management

| Number | Name | Description |
|--------|------|-------------|
| 1 | `EXIT` | Terminate the current process |
| 5 | `EXEC` | Load and execute an ELF binary; blocks until it exits, then returns its status |
| 7 | `SET_PRIORITY` | Set process scheduling priority |
| 51 | `GETPID` | Get the process ID of the caller |
| 52 | `SLEEP` | Block the caller for a number of **milliseconds** |
| 53 | `FORK` | Duplicate the caller; returns 0 in the child and its pid in the parent |
| 54 | `WAIT` | Collect a finished child's exit status; blocks if none has finished yet |
| 55 | `TIME` | Fill an `esd_time_t` with the current wall-clock time |
| 56 | `BRK` | Move the program break; returns the resulting break, so a refusal is the break unmoved |
| 57 | `MMAP` | Map anonymous, private, zeroed pages; returns the address or `0xFFFFFFFF` |
| 58 | `MUNMAP` | Release pages obtained from `MMAP`; refuses any range outside that region |
| 59 | `KLOG_CTL` | Inspect and control the kernel log: clear it, move the severity threshold, read the held and dropped counts |
| 60 | `SETPGID` | Place a process in a process group; the caller may move itself or a child |
| 61 | `TCSETPGRP` | Hand the terminal to a process group, which is what Ctrl-C reaches |
| 62 | `GETPGID` | Read a process's group |
| 99 | `YIELD` | Voluntarily yield the CPU |

`TIME` fills an `esd_time_t` (`include/esdtime.h`), shared verbatim with user space the
same way `esd_stat_t` is. The fields are broken down — year, month, day, hour, minute,
second — rather than a count of seconds since an epoch: the RTC reports them that way and
nothing in this system agrees on an epoch to count from. A non-zero second argument asks
for UTC instead of local time, which is what `date -u` passes; the shift between them is
done in the kernel so the calendar carry exists in one place.

`tz_offset_hours` says how far ahead of UTC the other fields are. It comes from
`/etc/timezone`, which holds a signed hour count rather than a zone name — there is no
timezone database here, and `Europe/Istanbul` would be a name nothing could look up. The
RTC itself is assumed to hold UTC, which is what QEMU presents by default; a machine whose
CMOS holds local time wants an offset of 0. There is no way to *set* the clock, and no
daylight saving.

`EXEC` returns a negative errno when the program could not be started, and otherwise
the child's exit status once it has finished — statuses are masked to 0-255, so the
two cannot be confused. It blocks the caller on `WAIT_CHILD` for the child's lifetime,
which is how it returns a status at all, and it predates `WAIT` rather than being
replaced by it.

`FORK` shares the caller's user pages copy-on-write rather than duplicating them: the
child maps the parent's own frames, both sides give up write access to anything that was
writable, and the first write from either splits that page. A page that was already
read-only is shared as it stands, so writing to a program's text is still an access
violation rather than a copy. Descriptors, working directory, uid, signal handlers,
priority and FPU state are carried over. It returns `E_AGAIN` when `MAX_TASKS` live tasks
already exist and `E_NOMEM` when the address space could not be built.

`WAIT` returns a child's status, or `E_CHILD` when the caller has none left to wait for —
an error rather than a block, so a parent that called it one time too many does not sleep
until the machine is rebooted. A child that finishes while its parent is busy leaves its
status in a fixed table until it is collected, which is what makes both orders work: the
parent arriving first and waiting, and the child finishing first.

`SLEEP` takes milliseconds rather than seconds because `TIMER_HZ` is 100, giving a
10 ms resolution that a seconds-only call could not reach; the shell's `sleep`
builtin still takes seconds and multiplies. A sleeping task holds `WAIT_TIMER`
with an absolute deadline in its PCB, and `schedule()` wakes it once that tick
passes — not `wakeup_tasks(WAIT_TIMER)`, which would wake every sleeper at once
regardless of what each one asked for.

### I/O and File Descriptors

| Number | Name | Description |
|--------|------|-------------|
| 3 | `READ` | Read from a file descriptor (stdin, pipe, file) |
| 4 | `WRITE` | Write to a file descriptor (stdout, pipe, file) |
| 36 | `PIPE` | Create an anonymous pipe (returns read/write FDs) |
| 37 | `DUP2` | Duplicate a file descriptor |
| 38 | `CLOSE` | Close a file descriptor |
| 40 | `OPEN` | Open a file and return a file descriptor |
| 49 | `FSTAT` | Report an open descriptor's metadata |
| 50 | `LSEEK` | Reposition an open file's read offset |

`READ` returns 0 for end-of-file from all three sources: a pipe whose last writer has
closed, a file read past its end, and the console once Ctrl-D has been typed. A `WRITE`
to a pipe with no readers left returns `-EPIPE` **and** raises `SIGPIPE` against the
caller, whose default action is to terminate it — a writer that means to survive has to
register `SIG_IGN` for it first and then check the return value.

### File System

| Number | Name | Description |
|--------|------|-------------|
| 8 | `CREATE_FILE` | Create a new file |
| 9 | `LIST_FILES` | List files in current directory |
| 11 | `CAT_FILE` | Read and display file contents |
| 22 | `RM_FILE` | Delete a file |
| 23 | `MV_FILE` | Rename a file |
| 26 | `MKDIR` | Create a directory |
| 28 | `LS_DIR` | List directory contents |
| 29 | `GET_DIR_ID` | Resolve directory path to ID |
| 34 | `CAT_RAW` | Read raw (unencrypted) file contents |
| 44 | `READDIR` | Read directory entries into user buffer |
| 45 | `SYNC` | Write dirty block-cache sectors out, and the kernel log to `/var/log` |
| 46 | `CHDIR` | Change the calling process's working directory |
| 47 | `GETCWD` | Write the working directory into a user buffer |
| 48 | `STAT` | Report a path's metadata into a user `esd_stat_t` |

Relative paths in every one of these resolve against the calling process's working
directory, which lives in the PCB. No syscall takes a base directory as an argument.

`STAT` and `FSTAT` fill an `esd_stat_t` (`include/stat.h`), shared verbatim with
user space. Two points are worth stating plainly:

- **`st_size` is not the size on disk.** Under `SEC_LEVEL_CRYPTO_ENFORCED` — the
  default — files are stored as an IV, a header and the plaintext padded to an AES
  block, so the directory table's length is always larger than what `read()`
  returns. `st_size` comes from `fs_size()`, which reads the real length out of the
  file's header; `st_disk_size` reports the stored length alongside it. `LSEEK`
  with `SEEK_END` uses the same source, so seeking to the end lands on the end of
  the data rather than somewhere inside the padding.
- **`st_size` is not authenticated.** For an encrypted file that length is read
  from the file's own header, and the HMAC that would vouch for it covers the
  plaintext and is only checked by `read()` over the whole file. Someone able to
  write to the disk can make `stat` report a wrong size; the read that follows
  fails, but the size on its own proves nothing.

There is no `st_mtime`. The on-disk directory entry carries no timestamps and the
RTC is not wired into the VFS, so any time reported here would be invented.

### IPC and Signals

| Number | Name | Description |
|--------|------|-------------|
| 2 | `IPC_SEND` | Send a message to another process |
| 6 | `IPC_RECEIVE` | Receive a message from mailbox |
| 18 | `ALARM` | Set a timer-based alarm |
| 24 | `SIGNAL_REG` | Register a signal handler, or `SIG_DFL` (0) / `SIG_IGN` (1) |
| 25 | `KILL` | Send a signal to a process |
| 27 | `SIGRETURN` | Return from signal handler |

Three signals terminate a process that has not handled them: `SIGKILL` (9), `SIGTERM`
(15) and `SIGPIPE` (13). The exit status is 128 plus the signal number, so a `wait()`
reporting 141 means the child was killed writing to a broken pipe. Every other signal is
recorded and dropped when no handler is registered.

A disposition is one of three things: an address to jump to, 0 for the default action, or
1 for `SIG_IGN`, which discards the signal. Dispositions are inherited across `fork()`
and reset to the default by `exec()` — which is why the shell can ignore `SIGPIPE` for
itself and still has to restore the default in each stage it forks.

### Security and Cryptography

| Number | Name | Description |
|--------|------|-------------|
| 13 | `LOCKDOWN` | Enter security lockdown mode |
| 33 | `SET_SEC_LEVEL` | Set kernel security level |
| 35 | `SETUID` | Change effective user ID (requires password) |
| 41 | `AUTH` | Authenticate user against shadow database |
| 43 | `GETUID` | Get user ID of current process |

*Note: Syscall numbers 30-32 are reserved for future crypto API.*

### System and Debug

| Number | Name | Description |
|--------|------|-------------|
| 10 | `CLEAR_SCREEN` | Clear the terminal |
| 12 | `SET_LAYOUT` | Switch keyboard layout |
| 14 | `STACK_DUMP` | Dump current stack (root only) |
| 15 | `MEMINFO` | Display memory usage statistics (root only) |
| 16 | `TEST_MALLOC` | Debug: test heap allocation |
| 17 | `HEXDUMP` | Hex dump memory region (root only) |
| 19 | `PANIC` | Trigger kernel panic (root only) |
| 20 | `REBOOT` | Reboot the system (root only) |
| 21 | `HALT` | Halt the CPU (root only) |
| 39 | `DMESG` | Copy a slice of the kernel log into a user buffer (root only) |
| 42 | `GET_ARGS` | Retrieve process command-line arguments |

---

## Security Model

esdumanOS implements a multi-layered security model:

### Security Levels

The kernel operates under one of four escalating security levels. Once raised, the level cannot be lowered.

```
Level 0: NORMAL             Standard operation. Encryption is optional.
                            |
Level 1: CRYPTO_ENFORCED    ALL VFS read/write operations MUST be encrypted.
                            Unencrypted disk access is denied.
                            |
Level 2: LOCKDOWN           New task creation is blocked.
                            The in-RAM master key is destroyed, and encrypted
                            VFS access is then refused outright rather than
                            silently attempted with a zeroed key.
                            |
Level 3: IMMUTABLE          Disk writes are completely disabled.
                            Kernel enters read-only mode. Dirty sectors already
                            in the block cache can still be flushed.
```

### Authentication

- User database stored in `/etc/shadow` in a `$v1$` format that records the
  iteration count and a 16-byte per-user salt alongside the derived key
- Passwords are derived with PBKDF2-HMAC-SHA256, not a bare hash. The stored
  iteration count is clamped to a sane range on read, so a tampered `/etc/shadow`
  cannot weaken verification or stall the kernel with an absurd count
- Verification is constant-time over the derived key
- Repeated failures are rate limited per process
- `su` command requires password re-authentication
- UID-based permission model (root = UID 0)
- `/etc/passwd` file is protected from non-root modification (delete, overwrite, rename)

### Disk Encryption

- AES-256-CBC, with the IV stored as the first 16 plaintext bytes of each file
- The ELF/filesystem master key is a **build-time constant** compiled into the
  kernel from `ESDUMAN_ELF_KEY_HEX`. This gives tamper resistance, *not*
  at-rest confidentiality: anyone holding the kernel binary holds the key, and
  the kernel says so in its own boot log
- Per-file IVs are derived rather than drawn directly from the entropy pool:
  `HMAC-SHA256(file key, "esdumanOS-iv-v1" ‖ counter ‖ pool bytes)`. The
  monotonic counter makes the input distinct on every call, so two files cannot
  share an IV even on a machine whose entropy sources are worthless; keying the
  derivation with the file key keeps the result unpredictable to anyone without it
- Magic header ("SAFE") and an HMAC-SHA256 tag over the plaintext for integrity

---

## Known Limitations

The following are known constraints of the current implementation. These are documented here for transparency and will be addressed in future releases.

### Resource Limits

| Resource | Limit |
|----------|-------|
| Maximum processes | 16 (`MAX_TASKS`) |
| File descriptors per process | 32 (`MAX_FD_PER_TASK`) |
| Files in directory table | 256 (`MAX_FILES_IN_DIR`) |
| Maximum filename length | 256 bytes (`MAX_FILENAME`) |
| Maximum disk size (FAT) | 2 MB (4096 sectors) |
| Physical memory supported | 128 MB |
| Pipe buffer size | 4 KB (`PIPE_SIZE`) |
| Pipes, system-wide | 16 (`MAX_SYSTEM_PIPES`, shared by named and anonymous) |
| Per-process kernel stack | 8 KB (`KERNEL_STACK_SIZE`) |
| Block cache | 64 sectors, 32 KB (`BCACHE_SIZE`) |
| Kernel log ring | 512 records, ~88 KB (`KLOG_RECORDS`) |

### Architectural

- **Single-core only.** SMP data structures are stubbed but not implemented.
- **The kernel is not preemptible.** A task that enters the kernel runs until it
  returns to user mode or blocks of its own accord; only Ring 3 frames are ever
  rescheduled. This is deliberate, and much of the kernel depends on it — the
  block cache, the pipe pool and the ATA driver hold no locks at all, the task
  list is edited without masking interrupts, and the uaccess fault state is a
  single global. Making the kernel preemptible means fixing all of that first,
  starting with the interrupt frame layout, which cannot currently describe a
  Ring 0 frame.
- **A pipeline is four stages at most**, and cannot be backgrounded. An external stage
  costs two tasks — the forked child plus the one `exec()` creates — and `MAX_TASKS` is
  16. Both limits are reported rather than reinterpreted.
- **`/etc` is written only on a fresh disk.** The system files live in the same block as
  the `/bin` tools, which runs when `init.elf` is absent, so an image carried over from an
  earlier version keeps its old `/etc`.
- **`/etc/profile` is a settings file, not a script.** Only `export KEY VALUE` is
  recognised. Running arbitrary commands from it would mean forking and exec'ing before
  the first prompt, and a syntax error in it would be a shell that will not start.
- **Job control stops at `&`, `jobs` and `wait`.** There is no `fg`, no `bg`, no
  Ctrl-Z and no process groups, so a background job can be waited for or killed but
  not brought back to the foreground. The shell tracks at most eight jobs.
- **Ctrl-C cannot interrupt a builtin.** A builtin runs inside the shell, and the
  shell ignores `SIG_INT` so that an interrupt at an idle prompt does not end the
  session — so `sleep 30` typed at the prompt runs to completion. External programs,
  and every stage of a pipeline, are in their own group and stop as expected.
- **A program that makes no system calls at all is interrupted late.** The default
  action for an unhandled signal is applied on the way out of a syscall, so a task
  spinning without one keeps its pending `SIG_INT` until it makes its next call. A
  registered handler is delivered at the next context switch either way.
- **No Ctrl-Z, no `fg`, no `bg`.** Stopping a job needs a task state the scheduler
  does not have yet; `jobs` and `wait` are the whole of job control.
- **`SIGPIPE` is the only signal a program receives without asking.** `SIGKILL` and
  `SIGTERM` still have to be sent with `kill`. There is no `SIGCHLD`, no `SIGALRM`
  delivered to user space, and no way to block a signal rather than ignoring it —
  the disposition is one of handler, default or ignore, with no mask.
- **`meminfo`, `hexdump`, `stack` and `free` print from inside the kernel**, so their
  output goes to the screen whatever the calling process's descriptor 1 points at. They
  cannot be piped or redirected: `meminfo > mem.txt` creates an empty file. `ls` and
  `dmesg` had the same defect and were moved off it in v0.5.3 — these four are root-only
  diagnostics and each needs a formatter of its own, so they were left.
- **FPU state is switched eagerly, and stays that way.** Every context switch saves
  and restores the full 512-byte register file rather than parking it behind CR0.TS
  and restoring it on first use. Lazy switching was on the roadmap as an
  optimisation and has been dropped on purpose: it is the mechanism behind LazyFP
  (CVE-2018-3665), which leaks FPU and SSE register contents across processes
  speculatively, and every major operating system moved back to eager switching in
  2018. One `fxsave` per switch across at most 16 tasks is the cheaper side of that
  trade by a wide margin.
- **`mmap()` is anonymous only.** There is no file backing and no `MAP_FIXED`: the
  kernel picks the address, the pages arrive zeroed, and nothing on disk is behind
  them. Mapping a file would need a page cache, which is a subsystem rather than a
  flag.
- **The program break never comes back down.** `umalloc()` reuses freed blocks but
  does not return the run to the kernel, because a run can only be released from the
  top and the top is rarely the part a program finished with. Allocations of 64 KB or
  more sidestep this by taking their own mapping, which `ufree()` releases exactly.
- **Pages are allocated eagerly.** `brk` and `mmap` both map every page before they
  return, rather than mapping on first touch. A program that reserves a large range
  and uses a little of it pays for all of it.
- **PIO disk access.** ATA driver uses Programmed I/O, not DMA. Single-sector transfers only.
- **No networking.** No TCP/IP stack, Ethernet driver, or socket API.
- **No dynamic linking.** All user-space programs are statically linked.
- **The log is written at checkpoints, not continuously.** `sync`, `halt` and
  `reboot` write `/var/log/kern.log`; a machine that loses power between them
  loses whatever was recorded since the last one. Appending is not possible in
  the current on-disk format — a file is one AES-CBC blob authenticated over its
  whole plaintext — so continuous writing and rotation wait on the format change.
- **`/dev/kmsg` keeps one cursor per process, not per open descriptor.** The
  device interface has no per-descriptor state to hang one on, so two descriptors
  in the same program share a read position. Linux keeps one per open.
- **No ACPI.** Shutdown and reboot use legacy keyboard controller reset.
- **VGA text mode only.** No framebuffer or graphical output.

### Security

- **The disk encryption key is compiled into the kernel image.** It provides
  tamper resistance, not confidentiality at rest. Anyone with the binary has the
  key. A real design would derive it from a passphrase at boot.
- **Entropy is weak without RDRAND.** The pool is fed by interrupt timing, and
  the only source it credits meaningfully is the keyboard — the PIT is periodic
  and earns nothing, and disk completions are capped at 64 bits for the whole
  boot. On a headless machine with no RDRAND the pool never reaches the threshold
  at which it would claim cryptographic quality, and it reports that honestly
  instead of pretending otherwise. IV and salt *uniqueness* does not depend on
  this; unpredictability does.
- **Entropy uniqueness is guaranteed within a boot, not across boots.** Two cold
  boots of the same image inside the same RTC second are not provably distinct.
  Closing that needs a seed persisted on disk.
- **No ASLR, no stack canaries in user space, no W^X for user pages.**
- LOCKDOWN blocks new programs and destroys the master key. It does not restrict
  an already-running shell, so a session that is open when the level is raised
  keeps its terminal.

---

## Roadmap

Near-term priorities for the project, roughly in order:

| Priority | Item |
|----------|------|
| P1 | Persist an entropy seed across boots, so uniqueness does not rest on the RTC |
| P1 | Bounded string operations throughout user space |
| P2 | Per-mutex wait queues, replacing the global `wakeup_tasks()` sweep |
| P2 | Derive the disk key from a boot passphrase instead of a build-time constant |
| P1 | `/var/log`: separate the log from the screen transcript, wrap the buffer, write it out |
| P2 | Setting the clock — the RTC is read and never written |
| P3 | ANSI escape code support in terminal |
| P3 | Expanded /dev device drivers |
| P3 | RISC-V port (the Makefile branch exists; `arch/riscv/` does not) |

Deliberately **not** planned: making the kernel preemptible. See the note under
[Architectural](#architectural) — the guarantee that kernel code runs to
completion is load-bearing for the block cache, the pipe pool, the ATA driver and
the uaccess state, and removing the guard without replacing all of that would
trade a documented limitation for silent corruption.

The following were on this list and are done: multi-block SHA-256 with correct
padding; supervisor-only kernel page permissions with `CR0.WP` enabled; address
space teardown on exit with frame reclamation; ELF segment address validation
(plus a fuzzer); BSS zeroing in the bootloader; block cache integration into the
VFS read/write path; salted password hashing, now PBKDF2-HMAC-SHA256; this syscall
reference; and moving the shell onto `fork`, which brought concurrent pipelines and job
control in v0.5.2 and `SIGPIPE` in v0.5.3.

---

## Contributing

Contributions are welcome. If you are interested in contributing to esdumanOS, please follow these guidelines:

### Getting Started

1. Fork the repository and create a feature branch from `main`.
2. Set up the build environment using the dependency list in the [Building](#building) section.
3. Run the full test suite before submitting changes:
   ```bash
   make test && make fuzz && make && make test_kernel
   ```

### Code Style

- **Language:** GNU C, freestanding (no libc). The build passes no `-std`, so it
  takes the compiler default and relies on GNU extensions throughout — inline
  `asm`, `__attribute__`, statement expressions. It does not compile under strict
  ISO mode and is not meant to. Assembly in NASM syntax.
- **Indentation:** Tabs for indentation, spaces for alignment.
- **Naming:** `snake_case` for functions and variables. `UPPER_SNAKE_CASE` for macros and constants.
- **Comments:** Comment non-obvious logic, and say *why* rather than restating the code. Comments throughout the tree are in English.
- **Headers:** Include the minimal set of headers required. Avoid pulling in `kernel.h` when a specific subsystem header suffices.

### Commit Messages

- Use concise, descriptive commit messages.
- Prefix with the subsystem: `mm:`, `fs:`, `proc:`, `drivers:`, `crypto:`, `tests:`, `build:`.
- Example: `mm: fix backward coalescing in kfree()`

### Pull Request Process

1. Ensure all tests pass (`make test && make test_kernel`).
2. Add or update tests for any new functionality.
3. Update documentation if the change affects user-visible behavior.
4. One feature or fix per pull request.

### Areas Where Help Is Needed

- Expanding the test suite (see Known Limitations).
- Writing user-space programs for `/bin`.
- Documentation improvements.
- Network stack implementation.
- Real hardware testing and driver development.

---

## License

This project is licensed under the MIT License. See [LICENSE](LICENSE) for details.

---

## Credits

**esdumanOS** is developed and maintained by [Esad Furkan Duman](https://github.com/iamfurkann).

### Acknowledgements

- [tiny-AES-c](https://github.com/kokke/tiny-AES-c) — AES implementation reference
- [OSDev Wiki](https://wiki.osdev.org/) — Invaluable resource for OS development
- [GRUB](https://www.gnu.org/software/grub/) — Bootloader
- [QEMU](https://www.qemu.org/) — Emulation and testing

---

<div align="center">

*A kernel built from first principles.*

</div>
