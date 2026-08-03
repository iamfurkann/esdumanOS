# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.1.0-pre-alpha] - 2026-08-03

### Added
- First public Pre-Alpha release of esdumanOS.
- Multiboot-compliant boot via GRUB with full GDT/IDT/TSS initialization.
- Physical memory manager (bitmap allocator, 128 MB addressable).
- Virtual memory with paging (recursive page directory, per-process address spaces).
- Kernel heap allocator with first-fit, coalescing, and corruption detection.
- Preemptive priority-based scheduler with kernel-mode preemption guard.
- ELF binary loader with PT_LOAD segments and user stack guard page.
- IPC: message passing (8-slot mailbox) and anonymous/named pipes.
- Signal subsystem (32 slots per process + kernel timer signals).
- Lazy FPU save/restore (FXSAVE/FXRSTOR).
- VFS with custom FAT-based file system, CRUD operations, atomic file updates.
- CryptoFS: transparent AES-256-CBC encryption with per-file random IVs.
- Block cache (64-slot LRU with write-through policy).
- DevFS with `/dev/null` and `/dev/random` device nodes.
- ATA/IDE PIO disk driver with IRQ-based waiting.
- PS/2 keyboard driver with US and Turkish layouts.
- VGA text mode with 3 virtual terminals (F1-F3), 80x100 scrollback buffer.
- RTC and serial port drivers.
- 42 system calls via INT 0x80.
- Multi-layered security model (NORMAL → LOCKDOWN → CRYPTO_ENFORCED → IMMUTABLE).
- User authentication via `/etc/shadow` with SHA-256 hashed passwords.
- AES-256 (ECB, CBC, CTR), SHA-256, HMAC, ChaCha20 cryptographic primitives.
- Unix-style shell with 20+ builtins, pipes, redirection, chaining, variable expansion.
- User-space programs: sh, hello, echo, clear, touch, rm, mv, cp, free, whoami, kill, grep, head, date.
- FHS directory layout created at boot (`/bin`, `/dev`, `/etc`, `/home`, `/root`, `/tmp`, `/var`).
- 19 kernel self-test modules covering VFS, memory, pipes, security, and more.
- Host-side tests, fuzzing (45 corpus files), and CI pipeline (GitHub Actions).
- Experimental RISC-V 64-bit skeletal support.
- MIT License.

### Fixed
- `kfree` triple fault crash during process exit by deferring active stack unmapping.
- Kernel memory leak in `exit_current_process` by properly reclaiming zombie task memory.
- Boot hangs caused by debug paging identity maps.

### Security
- Replaced hardcoded AES IV with dynamic `/dev/urandom` 16-byte generation.
- Removed plaintext `passwords.txt` from repository.

### Changed
- Modularized `kernel_main` into smaller init subsystems (boot, memory, fs, userspace).
- Adopted semantic versioning starting at v0.1.0-pre-alpha.
