<div align="center">

# esdumanOS

**A 32-bit x86 operating system kernel written from scratch in C and assembly.**

[![CI](https://github.com/iamfurkann/esdumanOS/actions/workflows/ci.yml/badge.svg)](https://github.com/iamfurkann/esdumanOS/actions/workflows/ci.yml)
![Version](https://img.shields.io/badge/version-1.14.0--beta.1-blue)
![Architecture](https://img.shields.io/badge/arch-x86__32-orange)
![Language](https://img.shields.io/badge/language-C%20%7C%20x86%20ASM-green)
[![License: MIT](https://img.shields.io/badge/license-MIT-purple)](LICENSE)
![Status](https://img.shields.io/badge/status-beta-yellow)
[![Website](https://img.shields.io/badge/Website-Live-2ea44f)](https://iamfurkann.github.io/esdumanOS-website/)

*An independent operating system, booting through GRUB via Multiboot,*
*with preemptive multitasking, an encrypted file system, and a Unix-style shell.*

</div>

> **⚠️ Beta Notice:** This is a development release intended for testing and educational
> purposes only. It is not suitable for production use. Expect bugs, crashes, and
> incomplete features.
>
> The kernel preserves published syscall numbers. v1.14 extends directory IDs
> to identify their volume and adds the root-only `MKFS` call at 73. Existing v3
> partitions keep their 16-bit on-disk IDs and boot through the same root path.
> `mount` (71) now attaches a second filesystem at `/mnt`, and `umount` (72)
> detaches it. The boot filesystem remains accessible at `/`.

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

This source tree targets **1.14.0-beta.1**, an experimental release. The boot
filesystem stays mounted at `/` while one independently keyed v3 filesystem is
attached at `/mnt`. Formatting is explicit; discovering or mounting a blank disk
does not initialize it. Existing v3 layouts remain readable, but old installed
`sh`, `mount`, `umount` and login programs need the [program refresh](#upgrading-an-existing-v3-root).

The implemented system includes copy-on-write `fork`, `wait`, process groups,
signals, pipelines, an editor, permission checks, PCI/AHCI/xHCI drivers, USB boot
keyboards and storage, BIOS/UEFI boot, an early framebuffer and ACPI shutdown.
There are 68 production syscalls and 23 programs in `/bin`, plus the login/init
program at `/init.elf`.

Validation covers host tests, 46 kernel-mode modules, a real Ring 3 payload,
BIOS/i440fx, q35/AHCI, OVMF/UEFI and RDRAND/SMEP/SMAP configurations. Production
CLI and power-cut experiments are separate from the default suite. Exact dated
results and incomplete runs are recorded in the
[reliability audit](docs/engineering/RELIABILITY_AUDIT.md); QEMU results do not
establish physical-hardware compatibility. Remote CI and publication are
separate release gates.

The v3 filesystem is not journaled. Some measured power cuts leave metadata
that the next mount refuses, and there is no automatic repair. Earlier long
production unlock timeouts also remain unexplained despite later successful
runs. See [Known Limitations](#known-limitations) and [SECURITY.md](SECURITY.md).
Release-by-release history belongs in [CHANGELOG.md](CHANGELOG.md).

---

## Features

### Kernel Core

| Component | Description |
|-----------|-------------|
| **Boot** | Multiboot 1/2 entry, 16 KB boot stack, temporary identity mapping and a higher-half kernel |
| **GDT / IDT / TSS** | 9-entry GDT with Ring 0 and Ring 3 segments, 256-vector IDT with PIC remapping, one TSS for privilege transitions and a second for the double-fault task gate |
| **Syscall Interface** | 68 system calls via INT 0x80, covering process control, job control, file I/O, IPC, security, and device access |
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
| **Job control** | Process groups, a foreground group the terminal points at, and a stopped task state. Ctrl-C interrupts the foreground job and Ctrl-Z parks it; `fg` and `bg` bring it back |
| **FPU** | Eager FPU state save/restore on every context switch (FXSAVE/FXRSTOR), per-process 512-byte state. Not lazy — there is no `CR0.TS` / `#NM` path |

### File System

| Component | Description |
|-----------|-------------|
| **VFS** | Custom FAT-based file system with flat directory table, CRUD operations, buffered whole-file replacement, and independent root/secondary volumes. Updates are not power-loss transactions. Sector 0 is an MBR partition table; the file system lives in a partition and its superblock carries a magic number, a format version and the partition-relative geometry it was laid out with. The allocation table counts 4 KB clusters. An unrecognised disk is refused rather than read, a disk that cannot be read is refused rather than formatted, and the directory table is checked against its own invariants before it is believed. The directory table and the allocation table are sized from the device at mount, up to 8192 entries and ~1 GB; entries are 96 bytes each, with an owner, a group, permission bits and metadata-change and modification times |
| **CryptoFS** | Transparent AES-256-CBC encryption layer. Per-file IVs are derived as `HMAC-SHA256(file key, label ‖ counter ‖ pool bytes)`, truncated to 16 bytes; distinct counter inputs do not guarantee collision-free output or prevent replay. HMAC-SHA256 authenticates file plaintext; metadata remains plaintext |
| **Block Cache** | 64 slots keyed by device and sector, with write-back at 32 dirty slots, on the yield-driven 5-second deadline, or on explicit `sync()`. Admission rejects invalid requests and non-512-byte sectors. Every accepted sector remains dirty on write failure; clean slots and other writable victims remain usable. A full cache returns `E_IO` after one attempt per dirty slot. `sync`, reboot, halt and mount changes report flush failures instead of discarding pending data. These policies are not power-loss atomicity or a hardware cache flush |
| **Block Device Layer** | A table of up to four registered devices under the cache, with the sector bounds check in one place rather than in each driver — which matters more with two disks than it did with one, because there are two sector counts to check against and two chances to forget. Devices are found by the name they publish, which is how `mount` is told which one to use, and two may not share one. A driver's errno reaches the file system unchanged, which is what lets the mount path tell a disk it cannot read from a disk with nothing on it |
| **DevFS** | `/dev/null`, `/dev/random`, `/dev/urandom` and `/dev/kmsg` device nodes; the random devices are ChaCha20 keyed from the kernel entropy pool and re-keyed periodically, and `/dev/kmsg` streams log records with a per-process cursor |

### Drivers

| Driver | Description |
|--------|-------------|
| **ATA/IDE** | PIO-mode disk I/O with IRQ-based waiting, 28-bit LBA, single-sector read/write, cache flush. Every wait is bounded, including the one after `IDENTIFY` that was not until v1.4.0 |
| **PCI** | Configuration space through ports 0xCF8/0xCFC, read and written; the buses behind bridges walked from a worklist; up to 32 functions recorded with their class, IDs and base address registers. Memory decoding and bus mastering are enabled for a device a driver claims |
| **AHCI (SATA)** | The disk on a machine with no IDE controller. One controller, one port, one command slot, one sector per command, polled rather than interrupt-driven. Registers as a block device exactly as the IDE driver does, so nothing in the file system knows which of the two it is on |
| **XHCI (USB)** | The bus a machine with no PS/2 controller puts its keyboard on. One controller, one interrupter, one event ring segment, polled. Resets the controller, takes it from the firmware where the firmware is holding it, installs the device context array and both rings, powers the ports and proves the rings work with a No Op command before believing any of it. Then it resets each connected port, gives its device a slot and an address, and reads the device and configuration descriptors — so `lsusb` reports vendors, products and classes rather than only ports. Where one of them is a boot keyboard it selects the configuration, opens the interrupt endpoint and queues a read; the event ring is then drained from the timer tick, a hundred times a second, by a poll that waits on nothing. Synchronous commands and EP0 transfers temporarily own the ring and match their exact queued TRB |
| **USB HID Keyboard** | Boot protocol only, which is what makes a report a fixed eight bytes and means no report descriptor has to be parsed. A report says which keys are held rather than which changed, so each one is read as a difference against the last; a rollover report is dropped whole rather than diffed. HID usages are translated into set-1 scancodes and handed to `keyboard_handle_scancode()`, so the Turkish layout, AltGr, the Ctrl fold, Ctrl-D and the arrow sequences all work on a USB keyboard without one line of the PS/2 driver changing. Unknown usages, including 0xE8–0xFF, are ignored before table access. The first boot keyboard on the selected bus supplies input |
| **USB Mass Storage** | Up to two sticks, each presented to the file system as a disk. Bulk-Only Transport - a 31-byte command wrapper out, the data, a 13-byte status wrapper back - and five SCSI commands on top of it: enough to learn the device is there, learn how big it is, and move a sector each way. Each stick carries its own transport frame, tag counter and capacity, reached through `blockdev_t`'s `ctx`. They register as `usb0` and `usb1`. Existing filesystems on them can become the boot fallback root, or one can be attached at `/mnt` while the root stays live |
| **PS/2 Keyboard** | IRQ1 handler, US and Turkish layouts, Shift/CapsLock/AltGr, 256-byte ring buffer |
| **Console** | Two backends behind one seam. VGA text mode writes cells to `0xB8000` and drives the hardware cursor; the framebuffer backend draws each cell as an 8x16 glyph into the linear pixel buffer the bootloader hands over, with a software cursor and a shadow so an unchanged cell is not blitted again. The terminal above them knows about neither |
| **VGA Text** | 3 virtual terminals (F1-F3 switching), 80x100 scrollback buffer, status bar, cursor management. 80x25 whichever backend is drawing it |
| **RTC** | CMOS real-time clock, BCD/binary and 12/24-hour conversion, with IRQ-state-preserving register access and clock-setting transactions |

### Cryptography

| Algorithm | Description |
|-----------|-------------|
| **AES-256** | Full implementation supporting ECB, CBC, and CTR modes (based on tiny-AES-c) |
| **SHA-256** | Incremental implementation (init/update/final), verified against NIST vectors |
| **HMAC-SHA256** | RFC 2104, verified against RFC 4231 test vectors. Used for file integrity and IV derivation |
| **PBKDF2-HMAC-SHA256** | Password key derivation with SHA-256 known-answer tests. Stored iteration counts outside the accepted range are rejected; production uses 600000 rounds by default |
| **ChaCha20** | Stream cipher backing `/dev/random` and `/dev/urandom`, keyed from the entropy pool |
| **Entropy pool** | RDRAND when available; otherwise interrupt timing jitter with per-source entropy budgets and a reported quality classification. A 32-byte seed in `/var/random-seed` mixes prior state into subsequent boots, credited zero bits; persistence or cloned-image rollback can reuse that state |

### User Space

| Component | Description |
|-----------|-------------|
| **Shell** | 35 builtins (cat, ls, cd, pwd, mkdir, rm, mv, write, env, export, exec, kill, su, sleep, dmesg, hexdump, help and more; four are deliberately absent from Tab completion and `help`, `panic` among them), four-stage pipelines, output redirection, `&&`/`\|\|` chaining, `$VAR`/`$?`/`~` expansion, line editing with history, and Tab completion that walks its candidates |
| **Programs** | 23 standalone ELF binaries: `sh`, `edit`, `hello`, `echo`, `clear`, `touch`, `rm`, `mv`, `cp`, `free`, `whoami`, `kill`, `grep`, `head`, `wc`, `date`, `stat`, `chmod`, `chown`, `lspci`, `lsusb`, `mount`, `umount` |
| **FHS Layout** | `/bin`, `/dev`, `/etc`, `/home`, `/root`, `/tmp`, `/var` and the `/mnt` attachment point |
| **Authentication** | Login handled by `/init.elf`, `/etc/shadow` password hashes, and `su` for switching to root |

### Testing and CI

| Layer | Description |
|-------|-------------|
| **Kernel Self-Tests** | 46 selectable kernel-mode modules plus Ring 3; `MODULE=<name>` or a comma-separated list filters a run. `crash-*` probes are opt-in and excluded from ordinary runs |
| **Host Tests** | Crypto/hash and ELF checks, deterministic RTC model, disk-image preservation, v3 metadata corruption cases, and terminal/QMP observation regressions |
| **Fuzzing** | Parser fuzz testing with 58 corpus files |
| **CI Pipeline** | GitHub Actions: host tests, fuzzing, OS build, QEMU kernel integration tests |

---

## Architecture

```text
GRUB (BIOS or UEFI, Multiboot 1/2)
    -> x86 boot and early framebuffer
    -> kernel: memory, scheduler, security, VFS and device drivers
    -> /init.elf: login and shell launcher (Ring 3)
    -> /bin/sh and other programs (Ring 3)
         -> INT 0x80: 68 production calls

File operations -> VFS -> per-volume CryptoFS/metadata
                      -> device-keyed block cache -> blockdev
                      -> ATA / AHCI / USB mass storage via xHCI
Keyboard input -> PS/2 or USB HID -> shared keyboard/terminal handling
Terminal output -> VGA cells or framebuffer glyphs; kernel diagnostics -> serial/log
```

### Virtual Memory Map

The kernel uses 32-bit non-PAE paging. The initial low identity mapping exists
only during boot; `init_paging()` replaces it with supervisor mappings in the
higher half. A numeric user address is not sufficient for access: the uaccess
helpers also validate its page mappings and permissions.

| Virtual address/range | Use |
| --- | --- |
| `0x00400000` up to `0xC0000000` | User address bounds; only allocated ELF, heap, mmap and stack pages are present |
| End of ELF image up to `0xA0000000` | Program break/heap growth |
| `0xA0000000` up to `0xAFFDF000` | Anonymous mmap region |
| `0xAFFDF000` | Unmapped 4 KB stack guard |
| `0xAFFE0000` up to `0xB0000000` | 32-page user stack, growing down |
| `0xC0000000` and above | Supervisor kernel space; first 16 MB map low physical memory at this offset, except physical page zero |
| `0xE0000000` | Temporary frame mapping |
| `0xE1000000` up to `0xE5000000` | 64 MB early framebuffer window |
| `0xE5000000` up to `0xF0000000` | 176 MB device mapping window |
| `0xFFC00000` / `0xFFFFF000` | Recursive page tables / page directory |

Endpoints marked “up to” are exclusive. The kernel is loaded at physical
1 MB; that does not imply an accessible low mapping after paging initialization.

### Boot Sequence

1. GRUB supplies Multiboot 1 or 2 information. Assembly establishes paging and
   the boot stack; the MB2 adapter supplies the kernel's internal boot structure.
2. Early framebuffer mapping precedes ordinary console and allocator setup.
   Serial/terminal, GDT, IDT, security/entropy and the embedded-image key follow.
3. PMM and permanent paging are initialized, preserving the early framebuffer;
   the double-fault handler, heap and 100 Hz PIT follow.
4. Kernel timers and keyboard are initialized, interrupts enabled, and PCI/ACPI
   probed. IDE is tried before AHCI; xHCI and USB storage register further disks.
5. An explicit `mkfs=<device>` selects a first-boot target. Otherwise existing
   filesystems are probed in boot-device/registry order. No readable filesystem
   means a diagnostic stop, not automatic formatting.
6. The selected root is unlocked with its disk passphrase, or gets a new key
   slot during explicit setup. First boot creates accounts and the FHS paths.
   Missing `/init.elf` triggers the installer for missing embedded programs and
   system text; existing files are not a package-update target.
7. System modes are applied after installation, the saved entropy seed is mixed
   and refreshed, and `/etc/timezone` is read.
8. FPU and multitasking are initialized; `/init.elf` runs in Ring 3, authenticates
   the user and launches `/bin/sh`. Self-test builds take their separate test path.

---

## Quick Start

Use an x86 Linux build environment with the [dependencies](#installing-dependencies)
below. The development workflow can edit on macOS and build/test inside a Linux
VM; native macOS and the unfinished RISC-V port are not supported build targets.

```bash
git clone https://github.com/iamfurkann/esdumanOS.git
cd esdumanOS
export ESDUMAN_ELF_KEY_HEX=$(openssl rand -hex 32)
make BUILD=prod
make run
```

`make run` serves VNC on **127.0.0.1:5901** and writes serial output to
`kernel_log.txt`. For a VM reached by the project's SSH forwarding, open a
second host terminal with `ssh -p 2222 -L 5901:127.0.0.1:5901 esduman@localhost`,
then connect a VNC viewer to `127.0.0.1:5901`. Adapt the SSH destination for
another build machine. Use a free local forwarded port if 5901 is already used.

On a newly created IDE image, edit the GRUB entry with `e`, append `mkfs=ata0`
to its kernel line, and boot with Ctrl-X or F10. For q35 use `mkfs=sata0`.
This explicit request only initializes an entirely zero-filled target.
Choose the disk passphrase, then separate account passwords for `root` and
`esduman`. There is no production default password. Later boots ask for the
disk passphrase before the login screen. Password input is limited to 63
characters; longer lines are rejected, and spaces are significant.

For terminal display, first build the ISO, run `make run_text` and choose the
GRUB **text-mode** entry. Curses cannot display the default framebuffer mode.

## Building

### Requirements

The maintained CI environment is x86 Linux on Ubuntu 24.04. The local
validation VM uses Debian 13 and Python 3.13. Exact minimum versions of every
tool are not independently tested; use the distribution toolchain rather than
the older minimum-version claims in historical documentation.

| Tools | Purpose |
| --- | --- |
| GCC with 32-bit multilib, GNU binutils, NASM, GNU Make | Freestanding x86 compilation and linking, `nm` for terminal observation |
| Clang with libFuzzer/AddressSanitizer | Parser fuzzing and sanitizer diagnostics |
| GRUB BIOS and x86-64 EFI modules, `grub-mkrescue`, `grub-file` | Hybrid BIOS/UEFI ISO and boot-header checks |
| xorriso, mtools | ISO/FAT image construction |
| `qemu-system-i386`, `qemu-system-x86_64`, OVMF | BIOS and 64-bit UEFI firmware hosting the 32-bit kernel |
| OpenSSL command and development library, `xxd` | Build key generation, ELF encryption and embedding |
| Python 3, coreutils (`timeout`, `dd`, hashes), Git | Host tests, metadata checker, QMP harness and development |

For fuzzing inside an emulated x86 VM, expose `POPCNT` (for example the VM's
`-cpu max` model) and check `grep -w popcnt /proc/cpuinfo`. `make fuzz` refuses
a host without it before starting libFuzzer. This is the build VM's CPU,
separate from the CPU model passed to a nested kernel test.

### Installing Dependencies

On Debian/Ubuntu x86 Linux:

```bash
sudo apt-get update
sudo apt-get install -y build-essential gcc-multilib binutils nasm clang \
    qemu-system-x86 grub-common grub-pc-bin grub-efi-amd64-bin ovmf \
    xorriso mtools openssl libssl-dev xxd python3 git
```

`bear` is optional for generating `compile_commands.json`. Other distributions
need equivalents for all these capabilities; their package names and firmware
paths are not validated here. UEFI test paths can be overridden with
`OVMF_CODE` and `OVMF_VARS`.

### Build Commands

`ESDUMAN_ELF_KEY_HEX` must contain exactly 64 hexadecimal characters. Keep the
same value during incremental builds. It is embedded in the kernel and is a
packaging parameter, not a disk password or a program-signing trust root.

```bash
export ESDUMAN_ELF_KEY_HEX=$(openssl rand -hex 32)
make -B BUILD=prod all
```

Use the forced build when changing that key or compiler flags: make does not
track environment-variable changes as dependencies. Ordinary source iteration
uses `make BUILD=prod`. The result is `myos.bin` and
`esdumanOS-v1.14.0-beta.1.iso`, with the version derived from `include/kernel.h`.
`make run` is not required to obtain the ISO.

Objects are separate under `build/prod`, `build/test` and `build/dev`, including
their libc archives. The final `myos.bin` and versioned ISO paths are shared by
production and `run-dev`, so rebuild explicitly with `BUILD=prod` after using
development mode before distributing an ISO. Test output is `myos_test.bin`.
Production uses 600000 PBKDF2 iterations; self-tests normally use 100000 and
`run-dev` uses 1000. Production rejects disks with reduced-cost key slots.
Use separate disposable disks/checkouts for dev mode; it reuses the same
interactive image names and does not convert a development disk for release.

`make clean` removes build outputs, including logs and disposable evidence
under `build/`, but preserves `disk.img`, `usb.img` and `usb2.img`. Keep evidence
you need outside that directory. `make reset-disk` deletes **only `disk.img`**;
the next boot still needs explicit `mkfs=ata0` (or the chosen actual device).
Only `ARCH=x86` is implemented; `ARCH=riscv64` stops with a diagnostic.

---

## Running

### QEMU (Recommended)

`make run` builds and boots using VNC; `make run_text` boots an already built
ISO in curses; `make restart` launches the existing ISO without rebuilding.
All use the same interactive images. `make debug` starts QEMU paused with a GDB
stub on port 1234; use `gdb -ex 'target remote :1234' myos.bin` to attach.

**First boot on a new disk.** At the GRUB menu press `e`, append
`mkfs=ata0` to the kernel command line for the disposable IDE disk in the
example below, and boot the edited entry. The kernel checks every sector for
zeroes before initializing this exact device. For the q35 AHCI configuration
use `mkfs=sata0`. Subsequent boots use the default menu entry and the disk
passphrase set on first boot. Existing v3 disks do not need conversion.

### Upgrading an Existing v3 Root

An existing root keeps its installed programs. To use the v1.14 `mkfs` builtin
and mount passphrase prompt, back up the image while QEMU is stopped, boot the
new ISO, and run these commands as root. They keep the previous programs and
system text under `.pre-v114` names. Stop if any command fails or a backup name
already exists. `/etc/shadow`, `/etc/passwd` and user files are preserved.

```text
mv /bin/sh /bin/sh.pre-v114
mv /bin/mount /bin/mount.pre-v114
mv /bin/umount /bin/umount.pre-v114
mv /etc/os-release /etc/os-release.pre-v114
mv /etc/motd /etc/motd.pre-v114
mv /init.elf /init.elf.pre-v114
sync
reboot
```

On the next boot, the missing init image triggers installation of the missing
embedded programs. Keep the offline backup until the upgraded commands and
files have been verified. This refresh requires free space for both copies.
It is not a disk-format migration or a power-loss-safe package transaction.


### Inspecting a Disk Image

For an offline raw disk image, `python3 tools/check_fs_image.py disk.img`
checks v3 metadata without writing or repairing anything. Stop the VM or
supply a consistent snapshot first. A successful check proves only metadata
structure, not file-content authentication or persistence through hardware
write caches. The checker rejects invalid parent chains, duplicate names,
FAT loops, shared clusters, missing file data and orphan allocations.
`make test_powercut` measures abrupt-stop outcomes using newly created images
and retains both serial logs and JSON results. It is an explicit diagnostic
outside `make test`; it does not operate on the interactive disk images.

### Production Storage Acceptance

The production keyboard/terminal check runs on disposable copies, with the
normal production KDF cost. On the Linux validation VM, build with
`make BUILD=prod` and run:

```bash
python3 tests/host/test_storage_boot.py \
    --iso esdumanOS-v1.14.0-beta.1.iso --kernel myos.bin \
    --firmware bios --refresh --long-password \
    --output "$HOME/v1.14.0-validation/production"
```

Use `--firmware uefi` for the OVMF/q35 path. `--legacy-kernel` accepts an
unmodified v1.13 production binary to create the old disk before exercising
the new ISO and program refresh; it excludes `--long-password`, because the
old login screen cannot accept setup's full 63-character password limit.
The harness needs QEMU, `nm` and the OVMF 4 MB firmware files for UEFI. It
records terminal transcripts, screenshots and a JSON report. Tests of a
production binary must not use a reduced-KDF self-test disk as their seed.
Completed stages are retained in `progress.json`; only `report.json` means
the entire scenario finished. A successfully provisioned image is preserved
as `provisioned-seed.img`. To check a saved legacy fixture without repeating
account setup, use `--seed IMAGE --expect-seed-version 1.13.0-beta.1 --refresh`.
This verifies the old version text and existing upgrade-probe payload before
refreshing programs. Every supplied image is copied before it is booted.
The long-password fixture ends in a space to verify that authentication keeps
the exact setup password. The explicit output directory above is outside the
source tree, so a later `make clean` does not discard validation evidence.



`make run` creates missing interactive images. It reuses existing images
across sessions, and `make clean` preserves them. The QEMU test matrix uses
its own disposable images.

### Attaching a Second Disk

 As root, run `mount` to list device names and sector
counts. To initialize a new, empty 4096-sector USB data disk named `usb0`,
use the following commands. `mkfs` prompts twice for the new disk passphrase.
For an existing v3 data disk begin with `mount usb0` and enter its passphrase.

```
mkfs usb0 4096
mount usb0
cd /mnt
pwd
cd /
umount
```

The second disk occupies the empty directory `/mnt`. An open file or working
directory on that disk makes `umount` return busy. Root-disk descriptors can
stay open. A failed flush keeps the secondary attached so writeback can be
retried. On remount the disk receives a new generation of directory handles.

The interactive Makefile topology includes an empty xHCI decoy, then an
active controller with a mouse on port 2 and data disks on ports 3 and 4.
Input uses PS/2. Missing `disk.img` and `usb.img` are created as 4096 sectors
(2 MiB), and `usb2.img` as 8192 sectors (4 MiB); existing contents are preserved.
Device names and capacities should be read from `mount`, not inferred from
host filenames. Never attach the same writable image to two QEMU processes.

### Manual QEMU Invocation

After building, `make disk.img usb.img usb2.img` creates only missing images.
This minimal BIOS example uses the root image and a graphical window; it does
not include the Makefile's USB test topology:

```bash
qemu-system-i386 -m 128 -boot d \
    -cdrom esdumanOS-v1.14.0-beta.1.iso \
    -drive file=disk.img,format=raw,if=ide,index=0,media=disk \
    -serial stdio
```

Keep `-boot d`: a formatted data disk has an MBR signature but no boot code.
Use a different serial file/display port when running independent guests.

### Booting the way a modern machine boots

UEFI uses an OVMF code image and a writable private copy of its variables.
This example also attaches USB data disks and a USB boot keyboard on explicit
root ports, so QEMU does not insert an unsupported hub:

```bash
esduman_ovmf_dir=$(mktemp -d)
cp /usr/share/OVMF/OVMF_VARS_4M.fd "$esduman_ovmf_dir/vars.fd"
qemu-system-x86_64 -M q35 -m 128 -boot d \
    -fw_cfg name=opt/ovmf/X-PciMmio64Mb,string=0 \
    -drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
    -drive if=pflash,format=raw,file="$esduman_ovmf_dir/vars.fd" \
    -cdrom esdumanOS-v1.14.0-beta.1.iso \
    -drive format=raw,file=disk.img,if=ide,index=0,media=disk \
    -device qemu-xhci,id=xhcidecoy -device qemu-xhci,id=xhci \
    -device usb-kbd,bus=xhci.0,port=1 -device usb-mouse,bus=xhci.0,port=2 \
    -drive id=usbstick,if=none,format=raw,file=usb.img \
    -device usb-storage,drive=usbstick,bus=xhci.0,port=3 \
    -drive id=usbstick2,if=none,format=raw,file=usb2.img \
    -device usb-storage,drive=usbstick2,bus=xhci.0,port=4 \
    -display vnc=127.0.0.1:1 -k en-us -serial file:kernel_log_uefi.txt
```

The firmware knob keeps PCI BARs below 4 GiB, which this non-PAE kernel can
address. It is an OVMF test configuration, not kernel BAR relocation or a
guarantee about another machine's firmware. Secure Boot is not supported.
Use `mkfs=sata0` only for explicit first setup of this blank root image.

### Booting a machine with no IDE controller

```bash
qemu-system-i386 -M q35 -m 128 -boot d \
    -cdrom esdumanOS-v1.14.0-beta.1.iso \
    -drive format=raw,file=disk.img,if=ide,index=0,media=disk \
    -display vnc=127.0.0.1:1 -k en-us -serial file:kernel_log_q35.txt
```

On q35, this `if=ide` drive is exposed through ICH9 AHCI; the kernel identifies
the usable disk as `sata0`. The minimal example has no USB data disks. To use
curses instead, choose GRUB's text-mode entry. A missing or unreadable root
stops with a diagnostic unless another registered device has a valid filesystem.

---

### Keyboard Shortcuts (Inside the OS)

| Key | Action |
|-----|--------|
| F1 / F2 / F3 | Switch between virtual terminals |
| Shift-PgUp / Shift-PgDn | Scroll terminal history |
| Arrows, Home, End, Delete, PgUp, PgDn | Sent to the program as escape sequences |
| AltGr | Access Turkish keyboard layout characters |
| Ctrl-D | End input for a program reading the keyboard (`cat`, `grep`, `head`, `wc`) |
| Ctrl-C | Interrupt the foreground job — every process in it, not just one |
| Ctrl-Z | Stop the foreground job and get the prompt back; `fg` or `bg` resumes it |

Ctrl with a letter produces that letter's control code, which is how Ctrl-D becomes the
end-of-file byte. Ctrl with anything else is passed through unchanged.

The navigation keys send what a terminal sends: `ESC [ A` through `ESC [ D` for the
arrows, `ESC [ H` and `ESC [ F` for Home and End, and `ESC [ 2 ~`, `ESC [ 3 ~`,
`ESC [ 5 ~`, `ESC [ 6 ~` for Insert, Delete and the Page keys. A sequence is placed in the
input ring whole or not at all, because a reader handed half of one would wait for a byte
that is not coming.

**Scrollback moved off the arrow keys in v0.8.2.** It had them because nothing else
wanted them; the arrows are now the only way a program can be told where to move, and a
key the driver consumes is a key no program will ever see. Shift with the Page keys is
where every terminal emulator puts scrollback.

Ctrl-C and Ctrl-Z are consumed by the driver rather than delivered as bytes: they name
the foreground process group and send it a signal. A program that wants the interrupt
itself asks for it by catching `SIG_INT`; leaving the byte in the input ring as well
would hand every reader a stray control character after every keypress.

**Ctrl-Z does not reach the guest under `-display curses`.** The key gets as far as the
terminal QEMU is running in — `stty susp undef; cat -v` echoes `^Z` there — but the
curses front end does not turn it into a guest keypress, and `stty susp undef` before
launching does not change that. Ctrl-C and Ctrl-D are delivered normally.

The symptom is unambiguous, because the `^Z` echo happens in the driver before
anything else: no `^Z` on screen means nothing arrived. Two ways to reach the same
place, both verified:

```
Esc, then 2          (QEMU monitor)
sendkey ctrl-z
Esc, then 1          (back to the guest)
```

or send the signal from inside the OS, which needs no monitor at all:

```
sleep 30 &
kill <pid> 20
jobs
fg
```

A graphical display backend delivers the key without any of this; curses is the one
that does not.

### Shell Commands

Once logged in, these commands are available. `date` is a `/bin` program;
the other commands in this list are shell builtins:

```
cat [-nbEsTA] <file>  Print file contents
ls [-l] [dir]         List a directory; defaults to the working directory
cd [dir]              Change directory; supports ., .., ~ and -
pwd                   Print working directory
mkdir <name>          Create directory
rm <path>             Remove a file or empty directory
mv <old> <new>        Rename a file within one directory
write <file> <text>   Create a file with the given contents
cat_raw <file>        Hex dump a file's stored bytes, bypassing decryption
env                   List environment variables
export KEY VALUE      Set environment variable (two words, not KEY=VALUE)
sleep <seconds>       Pause for a number of seconds
exec <program>        Execute an ELF binary
jobs                  List the jobs this shell started and still tracks
fg [%n]               Bring a job to the foreground and wait for it. With no
                      argument, the most recent one.
bg [%n]               Continue a stopped job without giving it the terminal
wait                  Block until every background job has finished
kill <pid> <signal>   Send a signal to a process (decimal). 9 and 15 terminate a
                      target that has not registered a handler for them; 20 stops
                      it and 18 continues it. A negative pid names a process group
                      and signals every member.
su                    Switch to root (prompts for the root password)
dmesg                 Display the kernel log
meminfo               Display memory usage (root only)
hexdump <addr>        Hex dump memory (root only)
stack                 Dump the current task's stack (root only)
date [-u]             Show the date and time; -u for UTC
date -s "..."         Set the clock, as "YYYY-MM-DD HH:MM:SS" (root only)
sync                  Retry metadata writes and flush both mounted volumes
setkey                Change the root disk passphrase (root only)
mkfs <device> <sectors> Initialize an unmounted, entirely blank disk (root only)
layout tr|us          Set the keyboard layout
lockdown              Enter the lockdown security level
help                  Show available commands
reboot                Reboot the system
halt                  Flush pending data and request ACPI poweroff; halt fallback
exit                  Exit the shell
```

`date`, `echo`, `clear`, `chmod`, `chown`, `mount` and `umount` are not builtins — they are ELF programs in `/bin`, reached
through the same path as any other program. `echo` does not implement `-n`. `panic`, `stack`, `testmalloc` and `alarm`
are debug builtins omitted from help/completion. `alarm` takes seconds;
`panic` deliberately stops the kernel. `rm`, `mv` and `kill` also have `/bin`
versions; an explicit path bypasses their builtin names.

**Tab completes, and Tab again walks.** The first press finishes a single match, or
narrows to as much as the candidates agree on, or lists what there is. Pressing Tab
again replaces the word with the next candidate and keeps going, wrapping at the end.
Any other key ends the walk — the list belonged to one word in one state of the line.

### Permissions

Every file and directory carries a mode, an owner and a group, and as of v0.9.1 they
are what decides access.

```
chmod 644 notes.txt        # owner reads and writes, everyone else reads
chmod 700 private          # owner only
chown 1000 notes.txt       # owner and group both become 1000 (root only)
chown 1000:1000 notes.txt  # the same, said explicitly
ls -l                      # mode, owner:group, size and modification time
```

Modes are octal only — no `u+x`. `chmod` is the owner's and root's; `chown` is root's
alone, because giving a file away is how a user escapes a quota on a system that has
one, and this restriction is easier to keep than to add back.

The rule is the Unix one, and it is asked twice. Reaching a file needs search permission
on every directory above it and read or write on the directory the operation happens in;
then the file's own bits are asked the same question. The matching class decides and only
that class: an owner with no permission is refused rather than falling through to the
group bits, which is what makes `chmod 077` mean what it says.

The execute bit decides what may be run. Before v0.9.4 that was decided by location —
anything in `/bin` for anybody, anything else for root alone — so a program's
executability was a property of where it sat. A program you wrote and `chmod 755`'d runs.

`/tmp` is `01777`. The sticky bit is what makes a world-writable directory survivable:
write permission on a directory is permission to remove things from it, so without it
anyone could delete anyone else's temporary file. With it, removal and renaming are
restricted to the owner of the entry, the owner of the directory, and root. `ls -l` shows
it as `t` in the last position.

The system's own paths are set at boot and put back if they drift: `/etc/shadow` and
`/var/random-seed` are `0600`, `/tmp` is `01777`, `/root` is `0700`, everything in `/bin`
is `0755`, and the rest of the top level is `0755`. Files are created `0644` and
directories `0755`.

### The Editor

`edit <file>` opens the system's text editor. It is modal, in the shape `vi` has: keys
are commands until `i` puts it in insert mode, and Escape brings it back. A file that
does not exist yet is not an error — it is a new file, and `:w` writes it.

| | |
|---|---|
| **Moving** | `h` `j` `k` `l` or the arrows, `0` and `$` or Home and End, `gg` and `G` for the first and last line, Page Up and Page Down |
| **Editing** | `i` insert here, `a` after the cursor, `A` at the end of the line, `o` and `O` open a line below or above, `x` delete a character, `dd` delete a line |
| **Undoing** | `u` takes back the last thing you did |
| **Searching** | `/<pattern>` finds it, `n` and `N` repeat forwards and backwards, `/` alone repeats the last pattern |
| **Commands** | `:w` write, `:w <name>` write as, `:q` quit, `:q!` quit without writing, `:wq` both, `:<number>` jump to a line |
| **Other** | Ctrl-L redraws, Ctrl-Z stops it and `fg` brings it back with the screen intact |

Ctrl-C is declined: throwing away an unsaved buffer should take more than one key, and
`:q!` is there for anyone who means it. A file is at most 64 KB, which is the most this
file system will write back.

`u` gives back a whole change rather than a byte: a sentence typed in insert mode is one
thing you did, so one press takes all of it. Each command in normal mode is its own step.
There is no redo — `u` keeps going backwards rather than toggling — and the log keeps 256
changes and 8 KB of deleted text, past which the oldest changes are given up and the
status line says so.

Search is plain text, not a pattern language, and it wraps in both directions: the status
line says when it went round the end, which is the only way to tell one match from the
same match found again.

A newline separates lines rather than ending them, so a file that ends in one shows an
empty line after it — where `vi` would show only the text. That is the honest reading for
an editor whose buffer is the file's bytes: pressing Enter at the end of the last line
produces exactly that newline, and a screen that did not show the new line would be
hiding the one thing the key just did.

It is the first program here to draw a full screen, and therefore the first consumer of
the escape sequences v0.8.0 taught the terminal and the keys v0.8.2 taught the keyboard.

**Operators:** Pipes (`cmd1 | cmd2 | cmd3`, up to four stages), output redirection
(`cmd > file`), chaining (`cmd1 && cmd2`, `cmd1 || cmd2`).

Four stages is a process budget rather than a preference: an external stage costs two
tasks, because the forked child runs the program through `exec()`, which creates a task of
its own. A fifth is refused with a message, as are a bare `|` and a backgrounded pipeline.

**Editing the line.** The left and right arrows move within the line and typing inserts
where the cursor is; Home and End jump to the ends, Delete removes forward and Backspace
back. The up and down arrows walk the last eight commands, and the line being typed is
kept while you do, so walking back returns it rather than an empty prompt. The password
prompt deliberately has none of this — there is nothing to navigate in a field displayed
as asterisks.

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

**Descriptor writes replace the whole file.** Bytes written through a descriptor are held
until the last descriptor closes and then committed as the file's entire new
contents, so there is no appending (`>>`), no writing into the middle of a file,
and no seeking during a write. That is not a shortcut: a stored file is a single
AES-CBC blob authenticated over its whole plaintext, so it can be replaced but
never extended in place. A single file is capped at 64 KB on this path. Check
the last `close()` result and `sync`: this is not a power-loss transaction.
Interrupted metadata writes can leave an image that the next mount refuses.

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

Run these commands in the Linux build environment with the build key exported.
They are the eight executable CI stages in [.github/workflows/ci.yml](.github/workflows/ci.yml):

```bash
make test
make fuzz
make BUILD=prod
make test_kernel
make test_kernel_q35
make test_kernel_uefi
make test_kernel QEMU_TEST_CPU="-cpu qemu32,+rdrand"
make test_smap
```

The BIOS, q35, UEFI and RDRAND runs execute all 46 kernel-mode modules and Ring
3, so their assertion totals should agree. SMEP/SMAP uses `-cpu max` and runs
the Ring 3 payload alone: its smaller total is intentional. The UEFI target
boots an ISO through GRUB/Multiboot 2; the other kernel targets use `-kernel`.

Every kernel target uses separate disposable images, an empty xHCI decoy,
an active controller with keyboard/mouse and two USB disks of different
capacities on explicit root ports. The image directories are under
`build/testrun` by default; `TESTRUN_ROOT=/absolute/path` keeps evidence elsewhere.
Different simultaneous invocations must use different roots. These targets do
not reuse the interactive images.

```bash
make test_kernel MODULE=usbkbd
make test_kernel MODULE=mount,vfs,bcache,ring3
make test_matrix QEMU_MATRIX_TIMEOUT=5400
make test_powercut QEMU_TEST_TIMEOUT=1800
```

`test_matrix` runs BIOS, q35, UEFI and SMEP/SMAP in parallel; **it does not
include the RDRAND run, host tests, fuzzing or production CLI acceptance**.
Default single-target timeout is 600 seconds; the matrix default is 1800.
Nested emulation may need larger observation deadlines. A timeout is not a
passing verdict and does not alone establish a kernel deadlock. Do not lower
KDF costs or remove assertions to make the gate finish.

`make test` runs crypto/hash and ELF checks, the real RTC driver against a
deterministic CMOS/IRQ model, interactive-image preservation, six metadata
test methods with corruption subcases, and nine terminal/QMP regressions.
`make fuzz` runs the ELF parser with libFuzzer/ASan for ten seconds and can add
generated corpus files; review them before staging. A bounded fuzz run is not
a proof of absence of parser defects.

`test_powercut` is opt-in: six scenarios stop after selected successful sector
writes or sync, preserve a snapshot, check metadata and reopen a copy. Invalid
metadata must be refused without writes; valid survivors are decrypted and
compared. It does not model torn sectors or a physical drive's volatile cache.
See the [production CLI procedure](#production-storage-acceptance) for normal
KDF, first-boot input, legacy refresh, two-volume persistence and ACPI poweroff.
Neither power cuts nor that production procedure currently runs in CI.
Exact results, hashes and earlier failures are in the
[reliability audit](docs/engineering/RELIABILITY_AUDIT.md).

---

### Kernel Test Modules

| Module | Coverage |
|--------|----------|
| `test_abi.c` | Published syscall numbers and constants, asserted by literal value: all 68 syscall numbers, 34 return codes (including `E_OK`), the `exec`/`wait`/`lseek`/`klog_ctl` constants, the signal numbers and the security levels — plus that each retired number (11, 28, 30, 31, 32, 99) is still answered with `E_NOSYS`. |
| `test_console.c` | Both console backends, against an array in RAM rather than a screen: that the framebuffer info sits where the Multiboot specification puts it, that a glyph is drawn pixel for pixel from the font, that foreground and background come out of the attribute byte, that a byte outside the font is drawn as a fallback box, that a cell the buffer does not have is not drawn at all, that a cell redrawn with what it already holds is skipped and one whose contents changed is not, that the cursor is painted over its cell and taken away again, and that a pixel format the backend cannot draw is refused without changing which backend is current |
| `test_ahci.c` | Device memory and the SATA driver, asserted so that the same assertions reach the same verdict on a machine that has an AHCI controller and one that does not: the four structures the controller reads out of memory are the sizes it indexes them by and fit one page with every alignment satisfied, a device mapping preserves its offset within the page and carries the cache-disable bit (read back out of the page table, because nothing else can see it), `pci_enable_device()` sets the two bits it is asked for and leaves the rest of the command register alone, and exactly one of the two storage drivers owns the disk — the one the bus says this machine has |
| `test_xhci.c` | Ring/context layout, MMIO register masks, bounded configuration descriptors, controller/device enumeration, and forced polling while synchronous command/EP0 completions are pending. |
| `test_usbmsc.c` | Bulk-Only Transport, and the disk it presents. Half of it is arithmetic about two structures and that half matters more than it looks: a command wrapper is 31 bytes and a status wrapper 13, and those are fields of the protocol rather than properties of the C - a device handed 32 bytes where it expects 31 stalls the endpoint, and the recovery for that is a reset this driver does not implement. So the sizes are asserted, the command block is asserted to start at byte 15, and the two signatures are asserted as the ASCII they are rather than against the constants they are defined as. Then the hardware: a stick was found and registered under the name `mount` uses, it reports a capacity and 512-byte sectors, it is **not** the root in this specific test topology - a sector reads back through the whole transport, and a sector past the end is refused before the device ever sees it. Then the second stick, which is why this driver has a table at all: both were brought up rather than only the first, each registered under a name and a `ctx` of its own, and **their capacities differ** - the test bench gives the two images deliberately different sizes, because a driver keeping the capacity in a single static would register the second stick with the first one's size and produce a disk that mounts and lies about where it ends |
| `test_mount.c` | RAM-backed multi-mount, blank-media protection, wrong-key refusal, targeted flush failure, retry and invalidation of stale handles |
| `test_acpi.c` | The tables, the sleep values, and the offsets into the FADT - almost all of it against tables built in the module rather than against the machine, because the cases worth asserting are the ones QEMU never produces. A checksum that does not hold is refused, and so is the *second* one, which only an ACPI 2.0 pointer has and which covers the half carrying the XSDT address. A declared length past the thirty-six byte copy the kernel keeps is refused rather than summed. `\_S5_` introduced by NameOp yields both sleep types shifted into place, the same four characters inside a string yield nothing - acting on that match would send an arbitrary value to a hardware register - and the walk is cut short at *every* offset in the block, not one, because every step is taken over bytes the firmware chose. Then the layout: four of the FADT offsets were wrong on the first attempt, each reading a neighbouring field and each returning a number that looked like an answer, so they are checked against the specification's own arithmetic - Flags is a doubleword, the reset register a twelve-byte generic address, and the fields from Flags to X_DSDT tile without a gap. One assertion asks the machine, and it has no branch in it, because an assertion with two arms reports a different count on different targets |
| `test_earlyfb.c` | The screen that has to exist before anything can complain about it, asserted without one. How many 4 MB entries cover a framebuffer - which is not a division: two megabytes starting three megabytes into a region straddles two entries, and a 1920x1080 panel is two entries aligned and three when it is not, a case no machine here will ever produce. Then every refusal in the order the boot meets them: no framebuffer reported told apart from a broken report, a text mode refused by name rather than drawn into, an address above 4 GB refused rather than truncated to a different one, and a depth, a pitch and a height the drawing code cannot use each rejected. Then a 4K panel fitting the window - which it did not before this release - and an 8K one refused rather than mapped short. Finally the two windows, which must meet exactly and not overlap, and the two colours a failed console can show, which must differ and must not be black |
| `test_usbkbd.c` | HID boot reports and the scancodes they turn into, almost all of it without a controller, a device or a register read: `usbkbd_report()` is handed eight bytes and told nothing about where they came from, so eight made up here are indistinguishable from eight a keyboard sent. High usages 0xE8–0xFF are rejected, exercised through presses/releases, and followed by a valid key. The table is also asked directly for a letter, for a navigation key that must carry its prefix or arrive as its keypad twin, for right alt which has to become AltGr or the Turkish layout loses a third of its characters, and for a usage set 1 cannot express — which must translate to nothing rather than to something wrong. Then the diff, which is where the shape of this protocol sets its trap: a report says what is held rather than what changed, so a key present in two reports must be pressed once, and a driver that emitted what it saw would repeat every held key a hundred times a second. A release types nothing and is therefore invisible at the ring, so it is proved through a modifier instead — a shift whose break code went missing would leave every letter after it capitalised. A rollover report is dropped whole and, the assertion that matters, leaves the previous report undisturbed. And one key end to end: Up arrives as the escape sequence xterm sends, through a driver that was never told about USB |
| `test_pci.c` | The bus walk and what it recorded: that something answered and that the count fits the table, that the host bridge reports a vendor while an address nothing decodes reads back all ones and is not stored as a device, that the stored vendor and class match a fresh read of the same registers rather than a consistent misreading of them, the lookups in both directions including one-past-the-end and a negative index, that no function above zero was recorded unless function zero announced itself as multifunction, and that enumerating twice describes the machine once |
| `test_blockdev.c` | The seam between the file system and its storage, against a device made up for the occasion: reads and writes reach the registered device at the sector asked for, a sector past its capacity is refused without the driver being called, a driver's errno arrives unchanged rather than flattened, a read-only device answers `E_ROFS` instead of calling a null handler, and with nothing registered both entry points answer `E_NODEV` |
| `test_keyslot.c` | The passphrase key slot, against the slot alone — no disk, no prompt: round trip, a wrong passphrase refused with the caller's buffer left untouched, every field of the slot edited in turn to confirm the tag covers the salt and IV as well as the ciphertext, iteration counts above and below what the build accepts, and the same data key wrapped under a second passphrase to prove a passphrase change preserves it |
| `test_vfs.c` | File/directory lifecycle, path limits, sector-tail copies, v3 geometry, FAT ownership/cycles/orphans, metadata refusal, multi-cluster contents and system-path permissions. |
| `test_memory.c` | Heap allocation, deallocation, read/write verification |
| `test_pipe.c` | Pipe creation, ring buffer, EOF detection, syscall integration |
| `test_security.c` | Authentication: wrong password, invalid UID, correct password. The permission rule the VFS decides with: the three classes, that the matching one is the only one consulted so an owner with no permission is not passed to the group, that every bit asked for has to be granted, and that root is subject to none of it |
| `test_passwd.c` | `/etc/passwd` protection: delete/overwrite/rename rejection |
| `test_adversarial.c` | Pointer validation: NULL, kernel space, upper bounds |
| `test_stress.c` | FD exhaustion (16 limit), long filename handling |
| `test_regression.c` | Previously fixed allocation, process-ID and storage-boundary regressions. |
| `test_integration.c` | Cross-component: VFS lifecycle, ELF load-to-process |
| `test_concurrency.c` | Hardware atomic operations (`__sync_lock_test_and_set`) |
| `test_devfs.c` | Device filesystem: `/dev` nodes, invalid device rejection, DRBG re-keying |
| `test_string.c` | libft string function correctness |
| `test_paging.c` | Map/unmap, collision rejection, CR0.WP enforcement on read-only user pages |
| `test_pmm.c` | Frame allocation, free-memory accounting |
| `test_lifecycle.c` | Address space clone and teardown; asserts every frame is reclaimed |
| `test_fault.c` | Double-fault infrastructure: task gate, TSS descriptor, dedicated stack |
| `test_syscall.c` | Dispatcher rejection of bad numbers, FDs, and sizes, and that the diagnostics render into the caller's buffer: that they report what they wrote, that a buffer too small is filled rather than overrun, and that one the caller does not own is refused |
| `test_process.c` | Scheduler, live-frame detection, rwlocks, syscall restart, idle task |
| `test_signal.c` | Handler registration and pending-signal bookkeeping |
| `test_jobctl.c` | Stopping and continuing a task: the state it remembers, who is told, and what happens to the terminal |
| `test_kbd.c` | Scancode translation: which keys become escape sequences, which are consumed, and that a sequence is written whole or not at all |
| `test_edit.c` | The line arithmetic `/bin/edit` is built on: where a line starts and ends, what a vertical move keeps, what deleting one takes with it, what a search finds and wraps past, and what undo gives back — including which changes it drops when its log runs out |
| `test_elf.c` | Loader validation: bad sizes, overflowing offsets, kernel load addresses |
| `test_crypto.c` | SHA-256 / HMAC / PBKDF2 against published vectors; CryptoFS round trip |
| `test_entropy.c` | Finite-sample extraction/IV/salt distinctness, per-source entropy budgets and failure policy. These checks do not prove collision-free or unpredictable output. |
| `test_bcache.c` | Two RAM-backed devices: complete cache-hit and persisted contents; high-water and timed write-back; invalid requests on hits and misses; failed reads; retention and recovery after `E_IO`, `E_NODEV`, `E_ROFS`, `E_NXIO` and `E_INVAL`; same-LBA device isolation; clean-victim progress, complete cache exhaustion and later-victim recovery; syscall error propagation through sync, reboot, halt, mount and unmount. The module restores fake-device state and preserves the root device |
| `test_time.c` | Calendar and epoch conversion, leap years, invalid-date refusal, TIME/SETTIME round trips, and 64 live RTC reads with IRQ delivery enabled. |

| `test_fork.c` | Address-space copy/share contents, isolation on writes and frame reclamation |
| `test_cow.c` | Read-only sharing, first-write splits, reference counts and TLB-sensitive isolation |
| `test_umem.c` | BRK/MMAP/MUNMAP bounds, zeroed mappings, refusal of unrelated ranges and frame accounting |
| `test_klog.c` | Record sequencing, severity, bounded text, rendering and ring retention |
| `test_tty.c` | Cursor movement, erase/scroll regions, colours and terminal escape parsing |
| `pgroup` in `test_process.c` | Process-group membership and terminal foreground ownership |
| `test_reap.c` | Child completion, wait/reap ordering, exit-status retention and task cleanup |

`test_fs_crash.c` supplies a separate sector-boundary crash probe for
`make test_powercut`; it is not a 47th normal kernel module.

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
|       |   |-- boot.asm             Multiboot 1 and 2 headers, page tables, stack
|       |   |-- multiboot2.c         The newer structure, translated into the older
|       |   +-- earlyfb.c            A screen before there is an allocator to make one
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
|   |   +-- signal.c                 Kernel timers and process signal support
|   |-- syscall/
|   |   |-- syscall.c                Dispatcher, 68 system calls
|   |   +-- sys_*.c                  Handlers by area: fs, ipc, process, mem, mount, sec, utils
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
|   |-- vfs.c                        VFS, per-volume FAT/directory tables, mount generations
|   |-- crypto_fs.c                  AES-256-CBC transparent encryption
|   |-- blockdev.c                   Registered block devices and bounded routing
|   |-- bcache.c                     Block cache (64-slot LRU, write-back)
|   +-- devfs.c                      Device filesystem (/dev/null, /dev/random, /dev/urandom, /dev/kmsg)
|
|-- drivers/
|   |-- ata.c                        ATA/IDE PIO disk driver
|   |-- pci.c                        PCI bus enumeration and configuration access
|   |-- acpi.c                       ACPI tables, and the one transition: power off
|   |-- ahci.c                       SATA disk driver (polled, one port)
|   |-- xhci.c                       USB controller: rings, ports, devices, endpoints
|   |-- usbkbd.c                     HID boot reports into set-1 scancodes
|   |-- usbmsc.c                     USB mass storage: Bulk-Only Transport, SCSI
|   |-- console.c                    Console backends: VGA text and framebuffer
|   |-- console_font.c               8x16 glyphs, ASCII 32-126
|   |-- keyboard.c                   PS/2 keyboard (US + Turkish)
|   |-- tty.c                        Backend-independent terminal and 3 virtual terminals
|   |-- serial.c                     Serial diagnostics
|   +-- rtc.c                        Real-time clock
|
|-- crypto/
|   |-- aes.c                        AES-256 (ECB / CBC / CTR)
|   |-- sha256.c                     SHA-256, incremental and one-shot
|   |-- hmac.c                       HMAC-SHA256
|   |-- pbkdf2.c                     PBKDF2-HMAC-SHA256
|   |-- keyslot.c                    Passphrase wrapping and authentication
|   +-- chacha20.c                   ChaCha20 stream cipher
|
|-- lib/                             Freestanding library
|   |-- stdio.c                      kvsnprintf, printk
|   +-- ft_*.c                       String, memory, character, list utilities
|
|-- apps/
|   |-- init.c                       Login and shell launcher
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
|       |-- date.c                   Display date and time, and set the clock
|       |-- edit.c                   Modal text editor, vi-shaped
|       |-- chmod.c                  Change permission bits
|       |-- chown.c                  Change owner and group
|       |-- lspci.c                  List the PCI devices found at boot
|       |-- lsusb.c                  List the USB controller and its ports
|       |-- mount.c                  List devices or unlock and attach one at /mnt
|       |-- umount.c                 Detach /mnt; leave root mounted
|       +-- stat.c                   Show a file's size, type, owner, mode and times
|
|-- include/                         Shared and subsystem headers
|   |-- kernel.h                     Master header, and where the version lives
|   |-- types.h                      Integer type definitions
|   |-- syscall.h                    68 production syscall definitions and test-only report call
|   |-- process.h                    Process control block, scheduler API
|   |-- fs.h                         VFS structures, file operations
|   |-- stat.h                       esd_stat_t and the lseek origins
|   |-- paging.h                     Virtual memory constants
|   |-- entropy.h                    Entropy pool API and quality contract
|   +-- security.h                   Security level enumeration
|
|-- tests/
|   |-- kernel/                      46 default modules, opt-in crash probe and framework
|   |-- user/                        Ring 3 test payload
|   +-- host/                        Host-side tests, fuzzing (58 corpus files)
|
|-- docs/engineering/                Dated reliability and validation evidence
|
|-- tools/                           Build and image-inspection utilities
|   |-- check_fs_image.py            Read-only v3 metadata checker
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

The kernel exposes 68 system calls through `INT 0x80`. The syscall number is passed in `EAX`.

Assigned numbers are preserved, and retired numbers **11, 28, 30, 31, 32 and
99 are never reused**. The highest production call is `MKFS` at 73; the next
unassigned production number is 74. Numbers 200 and above are reserved for
test-only calls and return `E_NOSYS` in production.

Number stability is not a claim that every historical binary has identical
behavior. v1.14 explicitly changes `MOUNT`/`UMOUNT` from root switching to
secondary attachment and adds a versioned passphrase form. Refresh the old
tools as described above. Runtime directory IDs are opaque 32-bit values;
on-disk IDs remain 16-bit in format v3. The numbers and constants are asserted
in `tests/kernel/test_abi.c`.

### Process Management

| Number | Name | Description |
|--------|------|-------------|
| 1 | `EXIT` | Terminate the current process |
| 5 | `EXEC` | Load and execute an ELF binary. Blocks until it exits and returns its status, or with `EXEC_NOWAIT` (1) in ecx returns its pid straight away |
| 7 | `SET_PRIORITY` | Set process scheduling priority |
| 51 | `GETPID` | Get the process ID of the caller |
| 52 | `SLEEP` | Block the caller for a number of **milliseconds** |
| 53 | `FORK` | Duplicate the caller; returns 0 in the child and its pid in the parent |
| 54 | `WAIT` | Collect what a child has to report; blocks unless `WNOHANG` (1). With `WUNTRACED` (2) a child that stopped is reported too, as `WSTATUS_STOPPED` (0x100) OR'd with the signal |
| 55 | `TIME` | Fill an `esd_time_t` with the current wall-clock time |
| 66 | `SETTIME` | Set the wall clock from an `esd_time_t`; the offset it carries is taken back out and UTC is stored. Root only |
| 56 | `BRK` | Move the program break; returns the resulting break, so a refusal is the break unmoved |
| 57 | `MMAP` | Map anonymous, private, zeroed pages; returns the address or `0xFFFFFFFF` |
| 58 | `MUNMAP` | Release pages obtained from `MMAP`; refuses any range outside that region |
| 59 | `KLOG_CTL` | Inspect and control the kernel log: clear it, move the severity threshold, read the held and dropped counts |
| 60 | `SETPGID` | Place a process in a process group; the caller may move itself or a child |
| 61 | `TCSETPGRP` | Hand the terminal to a process group, which is what Ctrl-C reaches |
| 62 | `GETPGID` | Read a process's group |
| 63 | `POLL` | Whether a read on a descriptor would block: 1 no, 0 yes. End of file counts as "no" |
| 67 | `YIELD` | Voluntarily yield the CPU. Assigned number 67 in v1.0.0; its earlier number 99 is retired |

`TIME` fills an `esd_time_t` (`include/esdtime.h`), shared verbatim with user space the
same way `esd_stat_t` is. The fields are broken down — year, month, day, hour, minute,
second — rather than a count of seconds since an epoch: the RTC and display consumers use calendar fields. File timestamps separately use
UTC seconds since the Unix epoch. A non-zero second argument asks
for UTC instead of local time, which is what `date -u` passes; the shift between them is
done in the kernel so the calendar carry exists in one place.

`tz_offset_hours` says how far ahead of UTC the other fields are. It comes from
`/etc/timezone`, which holds a signed hour count rather than a zone name — there is no
timezone database here, and `Europe/Istanbul` would be a name nothing could look up. The
RTC itself is assumed to hold UTC, which is what QEMU presents by default; a machine whose
CMOS holds local time wants an offset of 0. There is no daylight saving.

Setting the clock is `SETTIME` (66), added in v0.9.2 and restricted to root.
Invalid calendar fields are refused without changing the clock.

`EXEC` returns a negative errno when the program could not be started. In its
default mode it blocks on `WAIT_CHILD` and returns the child's exit status
(masked to 0–255). With `EXEC_NOWAIT`, it instead returns the new PID immediately;
use `WAIT` to collect the child's later status.

`FORK` shares the caller's user pages copy-on-write rather than duplicating them: the
child maps the parent's own frames, both sides give up write access to anything that was
writable, and the first write from either splits that page. A page that was already
read-only is shared as it stands, so writing to a program's text is still an access
violation rather than a copy. Descriptors, working directory, uid, signal handlers,
priority and FPU state are carried over. It returns `E_AGAIN` when `MAX_TASKS` live tasks
already exist and `E_NOMEM` when the address space could not be built.

`WAIT` returns the child's PID and writes its status through the supplied pointer,
or returns 0 for a nonblocking call with no result yet, or `E_CHILD` when the caller has none left to wait for —
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
| ~~11~~ | — | Was `CAT_FILE`; removed in v0.9.2. It printed a file from the kernel and had no caller. The number is permanently retired |
| 22 | `RM_FILE` | Delete a file |
| 23 | `MV_FILE` | Rename a file |
| 26 | `MKDIR` | Create a directory |
| ~~28~~ | — | Was `LS_DIR`; removed in v0.10.0. It printed a listing from the kernel and had no caller left after v0.9.2 moved `ls` onto `READDIR`. The number is permanently retired |
| 29 | `GET_DIR_ID` | Resolve directory path to ID |
| 34 | `READ_RAW` | Read an open file's stored bytes, without decrypting them. `read()` against the stored form: same descriptor, same offset, same shape |
| 44 | `READDIR` | Read directory entries into user buffer |
| 45 | `SYNC` | Write dirty block-cache sectors out, and the kernel log and the entropy seed to `/var` |
| 46 | `CHDIR` | Change the calling process's working directory |
| 47 | `GETCWD` | Write the working directory into a user buffer |
| 48 | `STAT` | Report a path's metadata into a user `esd_stat_t` |
| 64 | `CHMOD` | Set a path's permission bits. The owner and root, and nobody else |
| 65 | `CHOWN` | Set a path's owner and group. Root only |

Relative paths in every one of these resolve against the calling process's working
directory, which lives in the PCB. `READDIR` accepts the opaque directory ID returned by `GET_DIR_ID`;
path-based calls resolve from the PCB rather than a caller-supplied base directory.

`STAT` and `FSTAT` fill an `esd_stat_t` (`include/stat.h`), shared verbatim with
user space. Two points are worth stating plainly:

- **`st_size` is not the size on disk.** Under `SEC_LEVEL_CRYPTO_ENFORCED` — the
  default — an encrypted payload stores an IV, a header and plaintext padded
  to an AES block. Its stored length includes overhead beyond the plaintext
  returned by `read()`; a newly created empty entry can still have zero length. `st_size` comes from `fs_size()`, which reads the real length out of the
  file's header; `st_disk_size` reports the stored length alongside it. `LSEEK`
  with `SEEK_END` uses the same source, so seeking to the end lands on the end of
  the data rather than somewhere inside the padding.
- **`st_size` is not authenticated.** For an encrypted file that length is read
  from the file's own header, and the HMAC that would vouch for it covers the
  plaintext and is only checked by `read()` over the whole file. Someone able to
  write to the disk can make `stat` report a wrong size; the read that follows
  fails, but the size on its own proves nothing.

`st_mtime` and `st_ctime` arrived with the v0.9.0 format, as seconds since the Unix
epoch **in UTC**. `st_mtime` moves when the contents change and `st_ctime` when the entry
does, so a rename touches the second and not the first. `ctime` is not a creation
time. Zero is treated as unknown/unset and displayed as a dash; accepting a disk
format does not guarantee that every stored timestamp is nonzero.

The syscall reports UTC and `stat` displays local time, with the offset printed
alongside. Storage has to be UTC or the count is not an epoch: two files stamped either
side of a timezone change would otherwise compare by what the clock said rather than by
when it happened. Display does not — nobody reads their own files in UTC — so `/bin/stat`
asks the clock for the offset the system is set to and puts it back on, printing
`2026-08-24 12:00:00 +03` rather than the same instant as `09:00:00 UTC`.

`st_mode` holds permission bits used together with owner/group identity. `chmod`
sets them, `chown` sets the owner and group, and `ls -l` shows both.

### IPC and Signals

| Number | Name | Description |
|--------|------|-------------|
| 2 | `IPC_SEND` | Send a message to another process |
| 6 | `IPC_RECEIVE` | Receive a message from mailbox |
| 18 | `ALARM` | Arms, re-arms or cancels the caller's alarm; `ebx` is a count of seconds and 0 cancels. Returns the seconds left on the previous alarm, or 0 when there was none. `SIGALRM` (14) arrives when the deadline passes and terminates the process by default. An interval past ~249 days is refused with `E_INVAL`. Real as of v1.0.0 — before that it took no argument, sent no signal, and printed a line from inside the kernel |
| 24 | `SIGNAL_REG` | Register a signal handler, or `SIG_DFL` (0) / `SIG_IGN` (1) |
| 25 | `KILL` | Send a signal to a process, or to every member of a group when the pid is negative |
| 27 | `SIGRETURN` | Return from signal handler |

Five signals terminate a process that has not handled them: `SIGKILL` (9), `SIGTERM`
(15), `SIGPIPE` (13), `SIGINT` (2) and `SIGALRM` (14). The exit status is 128 plus the
signal number, so a `wait()` reporting 141 means the child was killed writing to a broken
pipe.

Two stop it instead: `SIGTSTP` (20), which is what Ctrl-Z sends, and `SIGTTIN` (21),
which a background job gets for trying to read the terminal. A stopped process keeps its
memory, its descriptors and the instruction it was on, and `SIGCONT` (18) puts it back.

`SIGCONT` is acted on where it is sent rather than where it is delivered, because a
stopped process never reaches a delivery point of its own. A process that registered a
handler is told as well, once it is running again: the default action needs no delivery,
but a full-screen program has to redraw what the shell wrote over its display while it
was away, and there is no other moment it could learn to.

Every other signal is recorded and dropped when no handler is registered.

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
| 68 | `SETKEY` | Change the root disk passphrase; `ebx` is the old one, `ecx` the new one. Root only. Re-wraps the data key under a fresh salt rather than re-encrypting the disk, so a wrong old passphrase returns `E_ACCES` and changes nothing |

*Note: 30-32 are retired, not reserved. They were set aside for a crypto API that was
never designed, and v1.0.0 retired them with the other holes — this line said "reserved
for future crypto API" for one release after that stopped being true.*

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
| 21 | `HALT` | Flush pending data and request ACPI S5; halt fallback if unavailable (root only) |
| 39 | `DMESG` | Copy a slice of the kernel log into a user buffer (root only) |
| 42 | `GET_ARGS` | Retrieve process command-line arguments |
| 69 | `PCIINFO` | Render the PCI inventory the kernel took at boot into `ebx`, capacity in `ecx` (root only). Reports what the enumeration recorded; it does not re-read configuration space |
| 70 | `USBINFO` | Render the USB controller, its ports and the devices addressed on them into `ebx`, capacity in `ecx` (root only). A snapshot taken at boot, like `PCIINFO`: there is no hotplug behind it. A machine with no xHCI controller gets one line saying so and a successful return |
| 71 | `MOUNT` | Root-only device listing or attach at `/mnt`. A caller supplies a passphrase pointer in ECX with `MOUNT_UNLOCK_V1` in EDX. Directory IDs and descriptors identify their volume |
| 72 | `UMOUNT` | Detach the secondary filesystem after writeback. An active CWD or open descriptor on that volume returns `E_BUSY`; the root stays mounted |
| 73 | `MKFS` | Initialize an unmounted blank device. Root supplies its name, a new passphrase and its exact sector count |

### Volume Administration ABI

All three calls are root-only and return a negative `E_*` on failure.

| Call/form | EBX | ECX | EDX | Success |
| --- | --- | --- | --- | --- |
| `MOUNT` (71), list | 0 | writable text buffer | capacity including NUL | bytes excluding NUL; list may be truncated |
| `MOUNT` (71), attach | NUL-terminated device name | passphrase string | `MOUNT_UNLOCK_V1` (`0x4d4e5401`) | 0; secondary published at `/mnt` |
| `UMOUNT` (72) | unused | unused | unused | 0; secondary flushed and detached |
| `MKFS` (73) | device name | new passphrase string | exact device sector count | 0; formatted but not attached |

A device name is at most 31 bytes; a passphrase is 1–63 bytes before NUL.
Without the MOUNT version marker, ECX is not dereferenced as a passphrase;
attaching a new encrypted volume therefore fails authentication. Naming the
already mounted root or secondary is a no-op success, not a root replacement.
`/mnt` must exist, be empty and not be a process's current working directory.
Detach returns `E_BUSY` for any live secondary FD/CWD, `E_NODEV` when absent,
and preserves the attachment on writeback failure. `MKFS` rejects mounted,
read-only, incorrectly sized, unreadable or nonblank devices; it never mounts
the result automatically. Device sectors must be 512 bytes.

IDs returned for secondary directories encode a mount generation in their
upper 16 bits. Treat them as opaque: never truncate them, persist them or
reuse them after detach. There are at most 32767 successful secondary
attachments per boot before the generation space is exhausted. Cross-volume
rename is rejected with `E_XDEV`; moving between different directories on the
same volume is also unsupported. Copy followed by verified removal is an
application operation, not an atomic move.

---

## Security Model

esdumanOS implements a multi-layered security model:

### Security Levels

The kernel operates under one of four escalating security levels. Once raised, the level cannot be lowered.

```
Level 0: NORMAL             Standard operation. Encryption is optional.
                            |
Level 1: CRYPTO_ENFORCED    Ordinary file content uses encryption. Metadata and
                            explicit raw reads remain outside this guarantee.
                            |
Level 2: LOCKDOWN           New task creation is blocked.
                            Both mounted data keys are destroyed, and encrypted
                            VFS access is then refused outright rather than
                            silently attempted with a zeroed key.
                            |
Level 3: IMMUTABLE          New VFS mutations are refused. Dirty sectors can still
                            be flushed. A prior LOCKDOWN is not undone and
                            destroyed keys are not restored.
```

### Authentication

- User database stored in `/etc/shadow` in a `$v1$` format that records the
  iteration count and a 16-byte per-user salt alongside the derived key
- Passwords are derived with PBKDF2-HMAC-SHA256, not a bare hash. The stored
  iteration count is rejected outside the accepted range. This bounds the
  requested KDF work, not its wall-clock time on a slow machine
- Verification is constant-time over the derived key
- Repeated failures are rate limited per process
- `su` command requires password re-authentication
- UID-based permission model (root = UID 0)
- `/etc/passwd` file is protected from non-root modification (delete, overwrite, rename)

### Disk Encryption

Each mounted volume has its own data key. PBKDF2-HMAC-SHA256 derives a
key-encryption key from that disk's passphrase; it unwraps the data key from a
116-byte key slot. The slot stores salt, iteration count, IV, wrapped key and
an HMAC. The HMAC input is exactly `salt || iv || wrapped_key`; the iteration
count selects the derivation and is range-checked before use. Production
defaults to 600000 rounds and accepts at most 10000000.

Files are stored with a 16-byte IV followed by an AES-256-CBC payload containing
the `SAFE` header, original length, plaintext HMAC and padded contents. The
HMAC authenticates file contents when read; it does not authenticate the
directory/FAT metadata or bind a file to a pathname. IVs are truncated from
HMAC over a label, monotonic counter and pool bytes. Distinct inputs are not a
mathematical guarantee of distinct output, especially across cloned or rolled
back images.

`setkey` rewraps the **root** data key without re-encrypting its files. There
is one slot per disk, no secondary-volume passphrase-change CLI and no recovery
password. There is no fallback to the embedded-image key. That build key only
packages the programs in the kernel; installing them re-encrypts their bytes
under the volume key. It cannot establish authenticity against someone who
can replace the kernel image. See [SECURITY.md](SECURITY.md) for entropy,
metadata, rollback and physical-access limits.

---

## Known Limitations

These are current implementation limits, not promises that every item will be
removed. Historical limitations and fixes remain in [CHANGELOG.md](CHANGELOG.md).

### Resource Limits

| Resource | Limit |
| --- | --- |
| Live processes / descriptors per task | 16 / 16 |
| User stack / per-task kernel stack | 128 KiB plus a guard page / 8 KiB |
| Physical memory managed | 128 MiB ceiling |
| Directory table | Up to 8192 slots **per volume**, including reserved entries; sized at format time |
| Filename / path | 63 / 255 bytes before NUL (`MAX_FILENAME=64`, `MAX_PATH=256`) |
| Buffered file write | 65536 bytes; whole-file replacement on last close |
| Filesystem allocation | 4 KiB clusters, at most 262144 clusters (about 1 GiB); larger devices are capped |
| Mounted filesystems | Two: boot root and one secondary at `/mnt` |
| Secondary mount generations | 32767 successful attachments per boot; no wrap/reuse |
| Block devices / cache | 4 registered devices / 64 sectors keyed by device and LBA |
| Pipes | 16 system-wide, 4096-byte buffer each |
| PCI functions / xHCI ports recorded | 32 / 32 |
| xHCI controller / addressed devices | One selected controller / 8 devices |
| xHCI slot array / scratchpad buffers | 32 slots / up to 64 buffers |
| USB keyboards / USB storage | One boot keyboard / two BOT/SCSI disks |
| USB events per timer poll | At most 16; nominal 100 Hz polling, not a latency guarantee |
| Framebuffer / device mapping windows | 64 MiB / 176 MiB; device bump allocation is not reclaimed |
| Kernel log | 512 records, about 88 KiB |
| `alarm` duration | Less than half the 32-bit 100 Hz tick range, about 249 days |
| File timestamps | Unsigned 32-bit UTC epoch seconds, ending in 2106 |

### Architectural

- **Single CPU, non-preemptible kernel.** User tasks are preempted; kernel
  execution depends on explicit blocking and return to user mode. SMP or kernel
  preemption needs changes to frame handling, synchronization and uaccess state.
- **Storage is not transactional.** Buffered writes commit on the last descriptor
  close; check that result and `sync`. There is no append, partial in-place write,
  write seeking or journal. Retry preserves pending metadata after ordinary I/O
  errors, but arbitrary power loss can leave orphan allocations and prevent mount.
  The offline checker diagnoses structure; it neither decrypts nor repairs.
  Successful device writes are not a hardware write-cache barrier.
- **No general mount tree or live root replacement.** `/mnt` is the only
  secondary attachment. Cross-directory rename is unsupported. Both volumes
  share the system-wide cache but have independent keys, tables and locks.
- **Existing root images keep installed programs.** A new ISO does not replace
  `/bin` automatically. Use the backed-up [refresh procedure](#upgrading-an-existing-v3-root).
  System configuration text is created by that installer, while permissions are
  reapplied every boot. `/tmp` cleanup removes direct files/empty directories;
  it is not recursive removal of arbitrary populated trees.
- **Four pipeline stages, no background pipelines.** External stages consume
  extra tasks because this `exec` creates a child; it does not replace the caller.
  There is no `>>`, shell scripting language or complete POSIX compatibility.
  `$?` is supported as a separate token, not interpolated inside another word.
- **Limited job control.** No `SIGSTOP` or `SIGTTOU`; a background reader gets
  `SIGTTIN`, but one that catches/ignores it can still read. Builtins run in the
  shell and cannot be interrupted/stopped like separate foreground programs.
  Curses may not forward Ctrl-Z; use VNC or the monitor workaround above.
- **Fixed terminal.** Three 80×25 virtual terminals, one status row and 24 text
  rows, ASCII font and integer scaling. The framebuffer requires a supported
  32-bit pixel layout below 4 GiB. Early output exists when that mapping succeeds;
  invalid/unreachable framebuffer reports can still leave no usable display.
- **Limited hardware support.** AHCI uses one controller/port/command slot and
  single-sector polled I/O. xHCI selects one controller; other controllers,
  external hubs, USB hotplug, UAS and BOT reset recovery are unsupported. PCI/USB
  inventory is a boot snapshot. DMA has no IOMMU isolation. BARs and framebuffers
  above 4 GiB are unreachable without a different memory/addressing design.
- **Limited ACPI.** Table parsing supports S5 poweroff and reset where available,
  not general AML, suspend/resume or ACPI interrupt routing. Shutdown/reboot
  refuse to discard failed writeback; poweroff has a halt fallback.
- **Clock and long waits.** CMOS reads/writes protect their index/data sequences
  against IRQ readers; this does not cover SMP or an NMI CMOS reader. RTC is
  assumed UTC. Timezone configuration is a fixed whole-hour offset, without DST.
  Some earlier production unlock observations exceeded 900 seconds; their cause
  is not established. Later successful BIOS/UEFI runs do not erase those failures.
- No network stack, SMP, RISC-V implementation or Secure Boot. Physical-machine
  acceptance is still separate work; QEMU USB tests are not hardware certification.

A production BIOS terminal run also observed foreground `mount` stop at its
passphrase prompt and return to the shell. Its cause is unresolved; the audit
retains the failure and repeat result. Do not enter a disk passphrase after the
shell prompt has returned. This observation is separate from the storage
metadata and kernel-suite results.

### Security

The system is educational, not suitable for protecting sensitive data. It has
no ASLR, stack canaries, IOMMU or verified boot. Read-only pages work, but
non-PAE paging has no NX bit for enforcing W^X. Entropy quality can remain
weak; counter/seed mixing does not prove cryptographic unpredictability or
collision-free output. Metadata and replay are outside file-content HMAC
coverage. LOCKDOWN wipes both mounted data keys and blocks new programs, but
does not erase all already decrypted process memory or terminate the session.
Read the full [security policy](SECURITY.md) before interpreting encryption as
a broader guarantee.

## Roadmap

The next hardware milestone is a recorded boot/login/write/sync/reboot/read/
shutdown cycle on named physical hardware. It needs explicit boot-media and
data-disk provisioning instructions, verified controller/keyboard support and
evidence of persistence on that machine. A hybrid ISO and two QEMU USB disks
alone do not complete that milestone.

Priority follow-up work includes a versioned transaction/recovery design for
power loss, a safe migration path if the format changes, diagnosis of the
remaining long unlock observations, and hardware coverage for controller
handoff, scratchpads, wider contexts and unsupported device topologies.

Completed foundations include COW `fork`/`wait`, file permission enforcement,
logging and entropy-seed persistence, PCI/AHCI/xHCI, USB keyboard/storage,
BIOS/UEFI entry, ACPI poweroff, early framebuffer diagnostics and independent
root/secondary mounts. New syscall allocations start at 74; retired numbers
stay retired. Features requiring a changed ABI or disk layout need explicit
compatibility/versioning decisions rather than reusing an old contract.

---

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) and [the release guide](docs/RELEASING.md)
for setup, code style, commit messages,
the complete CI matrix and release validation. Report reproducible non-security
defects through [GitHub Issues](https://github.com/iamfurkann/esdumanOS/issues).
Use the private reporting route in [SECURITY.md](SECURITY.md) for vulnerabilities.
Participation follows [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md).

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
