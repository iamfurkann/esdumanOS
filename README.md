<div align="center">

# esdumanOS

**A 32-bit x86 operating system kernel written from scratch in C and assembly.**

[![CI](https://github.com/iamfurkann/esdumanOS/actions/workflows/ci.yml/badge.svg)](https://github.com/iamfurkann/esdumanOS/actions/workflows/ci.yml)
![Version](https://img.shields.io/badge/version-3.4.3-blue)
![Architecture](https://img.shields.io/badge/arch-x86__32-orange)
![Language](https://img.shields.io/badge/language-C%20%7C%20x86%20ASM-green)
![License](https://img.shields.io/badge/license-MIT-purple)
![Status](https://img.shields.io/badge/status-alpha-red)
[![Website](https://img.shields.io/badge/Website-Live-2ea44f)](https://iamfurkann.github.io/esdumanOS-website/)

## Live Website

**https://iamfurkann.github.io/esdumanOS-website/**
---

*An independent operating system, booting through GRUB via Multiboot,*
*with preemptive multitasking, an encrypted file system, and a Unix-style shell.*

</div>

---

## Table of Contents

- [Overview](#overview)
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

---

## Overview

esdumanOS is a from-scratch operating system kernel for the x86 (IA-32) architecture. It does not derive from Linux, BSD, or any existing kernel codebase. Every subsystem, from the bootloader handoff through memory management, process scheduling, file system operations, and user authentication, is written specifically for this project.

The kernel boots via GRUB using the Multiboot specification, transitions through Protected Mode with full GDT/IDT/TSS initialization, sets up paged virtual memory, and launches a preemptive multitasking environment with Ring 0 / Ring 3 separation. User-space programs are loaded from ELF binaries, and the system provides a Unix-inspired shell with pipes, redirection, and environment variables.

A central design goal is treating security as a first-class concern rather than an afterthought. The kernel includes a tiered security level system, AES-256-CBC disk encryption, user authentication against a shadow password database, and permission enforcement at the system call boundary.

---

## Features

### Kernel Core

| Component | Description |
|-----------|-------------|
| **Boot** | Multiboot-compliant entry, 16 KB kernel stack, identity-mapped first 16 MB |
| **GDT / IDT / TSS** | 8-entry GDT with Ring 0 and Ring 3 segments, 256-vector IDT with PIC remapping, TSS for privilege transitions |
| **Syscall Interface** | 42 system calls via INT 0x80, covering process control, file I/O, IPC, security, and device access |
| **Kernel Logging** | 4 KB ring buffer logger (dmesg equivalent) with disk persistence to `/var/log/dmesg.log` |
| **Spinlocks** | Interrupt-safe kernel spinlock primitives |

### Memory Management

| Component | Description |
|-----------|-------------|
| **Physical Memory (PMM)** | Bitmap allocator, multiboot memory map detection, 128 MB addressable, next-fit optimization |
| **Virtual Memory (Paging)** | Recursive page directory mapping, per-process address spaces, 4 KB page granularity |
| **Kernel Heap** | First-fit allocator with block splitting, forward coalescing, magic-number corruption detection, double-free protection |

### Process Management

| Component | Description |
|-----------|-------------|
| **Scheduler** | Preemptive, priority-based, with kernel-mode preemption guard |
| **ELF Loader** | Loads PT_LOAD segments, sets up user stack with guard page, inherits file descriptors |
| **IPC** | Message passing (8-slot mailbox per process), anonymous and named pipes (16 pipes, 4 KB ring buffer each) |
| **Signals** | Per-process signal handlers (32 slots), kernel timer signals (16 slots) |
| **FPU** | Lazy FPU state save/restore (FXSAVE/FXRSTOR), per-process 512-byte state |

### File System

| Component | Description |
|-----------|-------------|
| **VFS** | Custom FAT-based file system with flat directory table, CRUD operations, atomic file updates |
| **CryptoFS** | Transparent AES-256-CBC encryption layer with per-file random IVs (RDRAND + PRNG fallback), integrity checksums |
| **Block Cache** | 64-slot LRU sector cache with write-through policy |
| **DevFS** | `/dev/null` and `/dev/random` device nodes |

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
| **SHA-256** | Hash function for password verification and file integrity |

### User Space

| Component | Description |
|-----------|-------------|
| **Shell** | Login screen, 20+ builtins (cat, ls, cd, pwd, mkdir, rm, mv, echo, env, export, exec, kill, su, dmesg, hexdump, help), pipe operator, output redirection, `&&`/`\|\|` chaining, `$VAR`/`$?`/`~` expansion |
| **Programs** | Standalone ELF binaries: `hello`, `echo`, `clear` |
| **FHS Layout** | `/bin`, `/dev`, `/etc`, `/home`, `/root`, `/tmp`, `/var` created at boot |
| **Authentication** | Password-protected login, `/etc/shadow` database, `su` for user switching |

### Testing and CI

| Layer | Description |
|-------|-------------|
| **Kernel Self-Tests** | 12 test modules: VFS, memory, pipe, security, passwd, devfs, regression, integration, adversarial, concurrency, stress, string |
| **Host Tests** | Crypto verification, ELF static analysis, hash validation |
| **Fuzzing** | Parser fuzz testing with 45 corpus files |
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
    |                    System Call Dispatcher (42 syscalls)           |
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
    |   |   GDT (8 entries)  |  IDT (256 vectors)  |  TSS  |  PIC  |   |
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
            |  User Stack             |  0xBFFFF000 (128 KB, guard page below)
            +                         +
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
  +---> init_gdt()               8-entry GDT, Ring 0 + Ring 3 segments
  +---> init_idt()               256 IDT entries, PIC remapping
  +---> init_pmm()               Bitmap allocator from multiboot memory map
  +---> init_paging()            Recursive page directory, identity map 16 MB
  +---> init_kernel_heap()       First-fit heap allocator
  +---> init_timer(1000)         PIT at 1000 Hz
  +---> init_signals()           Kernel timer signal table
  +---> init_security()          Master key derivation from boot parameter
  +---> ata_identify()           ATA disk detection
  +---> fs_init()                VFS, FAT, directory table from disk
  +---> init_fpu()               FPU/SSE detection and initialization
  +---> init_multitasking()      Idle task, task array
  +---> Create FHS hierarchy     /bin, /dev, /etc, /home, /root, /tmp, /var
  +---> Write passwd/shadow      Default user database on first boot
  +---> Load ELF programs        Decrypt and write init, hello, echo, clear
  +---> load_and_exec_elf()      Launch init process
  v
switch_to_user_mode() ---------> Ring 3, init shell starts
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

The kernel will boot in a QEMU window. You will be greeted with a login prompt.
Default credentials: `root` / `1234`.

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

### Installing Dependencies

**Ubuntu / Debian:**

```bash
sudo apt-get update
sudo apt-get install -y gcc-multilib nasm make qemu-system-x86 \
    grub-common grub-pc-bin xorriso mtools libssl-dev python3
```

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

```bash
# Full build: compile kernel, encrypt ELF binaries, create bootable ISO
make

# Clean all build artifacts
make clean

# Build for RISC-V 64-bit (experimental, skeletal support)
make ARCH=riscv64
```

The build process:
1. Compiles all C and assembly source files with `-ffreestanding -nostdlib`
2. Builds user-space ELF programs (init, hello, echo, clear)
3. Encrypts ELF binaries with AES-256 and embeds them as C arrays
4. Links the kernel binary against the custom linker script
5. Creates a GRUB-bootable ISO image via `grub-mkrescue`

---

## Running

### QEMU (Recommended)

```bash
# Build and boot in one step
make run
```

This executes QEMU with the following configuration:
- 128 MB RAM (`-m 128`)
- ISA debug exit device (for test automation)
- Serial output to stdio
- Bootable CD-ROM from the generated ISO
- Attached 2 MB raw disk image

### Manual QEMU Invocation

```bash
qemu-system-i386 \
    -m 128 \
    -cdrom esdumanOS.iso \
    -drive file=disk.img,format=raw,if=ide \
    -serial stdio \
    -device isa-debug-exit,iobase=0xf4,iosize=0x04
```

### Keyboard Shortcuts (Inside the OS)

| Key | Action |
|-----|--------|
| F1 / F2 / F3 | Switch between virtual terminals |
| Up / Down Arrow | Scroll terminal history |
| AltGr | Access Turkish keyboard layout characters |

### Shell Commands

Once logged in, the following builtins are available:

```
cat <file>          Print file contents
ls [dir]            List directory contents
cd <dir>            Change directory
pwd                 Print working directory
mkdir <name>        Create directory
rm <file>           Remove file
mv <old> <new>      Rename file
echo <text>         Print text
env                 List environment variables
export KEY=VALUE    Set environment variable
exec <program>      Execute ELF binary
kill <pid>          Send signal to process
su <user>           Switch user (requires password)
dmesg               Display kernel log
hexdump <addr>      Hex dump memory (root only)
help                Show available commands
reboot              Reboot the system
halt                Halt the CPU
clear               Clear screen
```

**Operators:** Pipes (`cmd1 | cmd2`), output redirection (`cmd > file`), chaining (`cmd1 && cmd2`, `cmd1 || cmd2`).

**Variables:** `$VAR` expansion, `$?` last exit code, `~` home directory.

---

## Testing

esdumanOS includes a multi-layered test infrastructure:

```bash
# Run host-side unit tests (crypto, ELF analysis, hash)
make test

# Run parser fuzzing with 45 corpus files
make fuzz

# Boot kernel in self-test mode, run 12 test modules in QEMU
make test_kernel
```

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
| `test_devfs.c` | Device filesystem: `/dev` directory, invalid device rejection |
| `test_string.c` | libft string function correctness |

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
|       |   |-- gdt.c                Global Descriptor Table (8 entries)
|       |   |-- idt.c                Interrupt Descriptor Table, PIC remapping
|       |   |-- isr.c                Interrupt dispatcher, exception handlers
|       |   |-- timer.c              PIT configuration (1000 Hz)
|       |   |-- tss.c                Task State Segment
|       |   +-- user_mode.asm        Ring 0 -> Ring 3 transition via iret
|       +-- linker.ld                Kernel linker script (load at 1 MB)
|
|-- kernel/
|   |-- core/
|   |   |-- kernel.c                 Entry point, subsystem initialization
|   |   +-- klog.c                   Ring buffer kernel logger
|   |-- proc/
|   |   |-- process.c                Scheduler, IPC, mutexes, context switch
|   |   |-- elf.c                    ELF binary loader
|   |   |-- pipe.c                   Anonymous and named pipes
|   |   +-- signal.c                 Timer-based kernel signals
|   |-- syscall/
|   |   +-- syscall.c                42 system call handlers
|   +-- security/
|       |-- security.c               Security levels, master key derivation
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
|   |-- bcache.c                     Block cache (64-slot LRU)
|   +-- devfs.c                      Device filesystem (/dev/null, /dev/random)
|
|-- drivers/
|   |-- ata.c                        ATA/IDE PIO disk driver
|   |-- keyboard.c                   PS/2 keyboard (US + Turkish)
|   |-- tty.c                        VGA text mode, 3 virtual terminals
|   +-- rtc.c                        Real-time clock
|
|-- crypto/
|   |-- aes.c                        AES-256 (ECB / CBC / CTR)
|   +-- sha256.c                     SHA-256 hash function
|
|-- lib/                             Freestanding standard library (49 files)
|   |-- stdio.c                      kvsnprintf, printk
|   +-- ft_*.c                       String, memory, character, list utilities
|
|-- apps/
|   |-- init.c                       User-space shell and login
|   +-- bin/
|       |-- hello.c                  Hello World ELF program
|       |-- echo.c                   Echo command
|       +-- clear.c                  Clear screen
|
|-- include/                         37 header files
|   |-- kernel.h                     Master header (version 3.4.3)
|   |-- types.h                      Integer type definitions
|   |-- syscall.h                    42 syscall number definitions
|   |-- process.h                    Process control block, scheduler API
|   |-- fs.h                         VFS structures, file operations
|   |-- paging.h                     Virtual memory constants
|   +-- security.h                   Security level enumeration
|
|-- tests/
|   |-- kernel/                      12 in-kernel test modules + framework
|   +-- host/                        Host-side tests, fuzzing (45 corpus files)
|
|-- tools/                           Build-time utilities
|   |-- mkfs.c                       File system image creator
|   |-- encrypt_tool.c               ELF encryption tool
|   +-- inject.c                     Binary data injector
|
|-- grub/
|   +-- grub.cfg                     GRUB bootloader configuration
|
+-- Makefile                          Build system (x86 + RISC-V)
```

---

## System Call Reference

The kernel exposes 42 system calls through `INT 0x80`. The syscall number is passed in `EAX`.

### Process Management

| Number | Name | Description |
|--------|------|-------------|
| 1 | `EXIT` | Terminate the current process |
| 5 | `EXEC` | Load and execute an ELF binary |
| 7 | `SET_PRIORITY` | Set process scheduling priority |
| 99 | `YIELD` | Voluntarily yield the CPU |

### I/O and File Descriptors

| Number | Name | Description |
|--------|------|-------------|
| 3 | `READ` | Read from a file descriptor (stdin, pipe, file) |
| 4 | `WRITE` | Write to a file descriptor (stdout, pipe, file) |
| 36 | `PIPE` | Create an anonymous pipe (returns read/write FDs) |
| 37 | `DUP2` | Duplicate a file descriptor |
| 38 | `CLOSE` | Close a file descriptor |
| 40 | `OPEN` | Open a file and return a file descriptor |

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

### IPC and Signals

| Number | Name | Description |
|--------|------|-------------|
| 2 | `IPC_SEND` | Send a message to another process |
| 6 | `IPC_RECEIVE` | Receive a message from mailbox |
| 18 | `ALARM` | Set a timer-based alarm |
| 24 | `SIGNAL_REG` | Register a signal handler |
| 25 | `KILL` | Send a signal to a process |
| 27 | `SIGRETURN` | Return from signal handler |

### Security and Cryptography

| Number | Name | Description |
|--------|------|-------------|
| 13 | `LOCKDOWN` | Enter security lockdown mode |
| 30 | `CRYPTO_ENCRYPT` | Encrypt data with kernel master key |
| 31 | `CRYPTO_DECRYPT` | Decrypt data with kernel master key |
| 32 | `CRYPTO_KEYGEN` | Generate cryptographic key material |
| 33 | `SET_SEC_LEVEL` | Set kernel security level |
| 35 | `SETUID` | Change effective user ID (requires password) |
| 41 | `AUTH` | Authenticate user against shadow database |

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
| 39 | `DMESG` | Read kernel log ring buffer |
| 42 | `GET_ARGS` | Retrieve process command-line arguments |

---

## Security Model

esdumanOS implements a multi-layered security model:

### Security Levels

The kernel operates under one of four escalating security levels. Once raised, the level cannot be lowered.

```
Level 0: NORMAL             Standard operation. Encryption is optional.
                            |
Level 1: LOCKDOWN           New task creation is blocked.
                            Terminal access is restricted.
                            |
Level 2: CRYPTO_ENFORCED    ALL VFS read/write operations MUST be encrypted.
                            Unencrypted disk access is denied.
                            |
Level 3: IMMUTABLE          Disk writes are completely disabled.
                            Kernel enters read-only mode.
```

### Authentication

- User database stored in `/etc/shadow` with SHA-256 hashed passwords
- `su` command requires password re-authentication
- UID-based permission model (root = UID 0)
- `/etc/passwd` file is protected from non-root modification (delete, overwrite, rename)

### Disk Encryption

- AES-256-CBC with per-file random initialization vectors
- Master key derived from boot-time `kernel_pass` parameter
- RDRAND hardware entropy source with PRNG fallback for IV generation
- Magic header ("SAFE") and djb2 checksum for integrity verification

---

## Known Limitations

The following are known constraints of the current implementation. These are documented here for transparency and will be addressed in future releases.

### Resource Limits

| Resource | Limit |
|----------|-------|
| Maximum processes | 16 |
| File descriptors per process | 16 |
| Files in directory table | 32 |
| Maximum filename length | 24 bytes |
| Maximum disk size (FAT) | 2 MB (4096 sectors) |
| Physical memory supported | 128 MB |
| Pipe buffer size | 4 KB |
| Named pipes | 16 |
| Kernel log buffer | 4 KB |

### Architectural

- **Single-core only.** SMP data structures are stubbed but not implemented.
- **No fork() syscall.** Process creation is exec-only; child processes do not inherit parent memory.
- **No mmap() or brk().** User-space programs cannot dynamically allocate memory beyond their initial ELF segments and stack.
- **PIO disk access.** ATA driver uses Programmed I/O, not DMA. Single-sector transfers only.
- **No networking.** No TCP/IP stack, Ethernet driver, or socket API.
- **No dynamic linking.** All user-space programs are statically linked.
- **No ACPI.** Shutdown and reboot use legacy keyboard controller reset.
- **VGA text mode only.** No framebuffer or graphical output.

### Security

- Default root password is `1234` and should be changed after first boot.
- Password hashing does not use per-user salt.
- Key derivation uses a custom KDF, not a standard algorithm (PBKDF2, scrypt).
- Boot-time encryption key is visible in the GRUB configuration.

---

## Roadmap

Near-term priorities for the project, roughly in order:

| Priority | Item |
|----------|------|
| P0 | Correct SHA-256 implementation (multi-block, proper padding) |
| P0 | Fix kernel page permissions (supervisor-only for kernel memory) |
| P0 | Process memory cleanup on exit (page table walk + frame deallocation) |
| P1 | ELF loader segment address validation |
| P1 | BSS zeroing in bootloader |
| P1 | Integrate block cache into VFS read/write path |
| P1 | Bounded string operations throughout user space |
| P2 | Per-mutex wait queues (replace global wakeup) |
| P2 | Salted password hashing |
| P2 | Syscall API reference documentation |
| P3 | Fork/wait syscalls for proper process hierarchy |
| P3 | ANSI escape code support in terminal |
| P3 | Expanded /dev device drivers |

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

- **Language:** C99, freestanding (no libc). Assembly in NASM syntax.
- **Indentation:** Tabs for indentation, spaces for alignment.
- **Naming:** `snake_case` for functions and variables. `UPPER_SNAKE_CASE` for macros and constants.
- **Comments:** Comment non-obvious logic. Existing comments are in Turkish; new contributions may use English.
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

<div align="center">

**esdumanOS** is developed and maintained by [Esad Furkan Duman](https://github.com/iamfurkann).

*A kernel built from first principles.*

</div>
