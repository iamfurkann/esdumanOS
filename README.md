# esdumanOS

[![CI](https://github.com/iamfurkann/esdumanOS/actions/workflows/ci.yml/badge.svg)](https://github.com/iamfurkann/esdumanOS/actions/workflows/ci.yml)

A 32-bit x86 operating system kernel written from scratch in C and x86 assembly, booting through GRUB via the Multiboot specification.

## Overview

esdumanOS is an independent, from-scratch operating system project, built without relying on any existing kernel codebase or framework beyond GRUB for the initial boot handoff. The project exists to engage directly with how a computer works beneath the layers of abstraction that most software development takes for granted: how a keystroke reaches the processor, how memory is claimed and protected, how a program is loaded and executed, and how a file system decides where data physically lives on a disk.

The project is not intended purely as an academic exercise. Alongside the core kernel work, it incorporates a security-first design philosophy, a disk-level encryption layer, and a Unix-inspired user and directory model. The longer-term goal is to move the kernel beyond emulation and have it run on real, domestically produced development hardware, rather than remain confined to a virtual machine.

## Design goals

- Build every core subsystem from first principles, with minimal reliance on external libraries
- Treat security as a foundational design constraint rather than an afterthought
- Follow established operating system conventions, such as a Unix-like directory layout and shell behavior, where they clarify the design, while making independent decisions where they do not
- Maintain a test suite and a continuous integration pipeline alongside the kernel itself, rather than relying solely on manual verification
- Keep the codebase organized in a way that scales as new subsystems are added

## Architecture

The kernel is organized around a small number of cooperating subsystems.

**Memory management.** A physical memory manager backed by a bitmap allocator, paired with virtual memory support through x86 paging, giving each process its own isolated address space.

**Process management.** A preemptive, priority-based scheduler, a loader for running compiled user-space programs, and inter-process communication through pipes and signals.

**File system.** A custom on-disk file system with directory support, layered underneath a security module that transparently encrypts data at rest using AES-256, so that the contents of the disk image are unreadable without the correct key.

**Security model.** A tiered security level system, a user identity and authentication model backed by an on-disk user database, and permission checks enforced at the system call boundary rather than left to individual callers.

**User space.** A shell inspired by the Unix shell tradition, a small set of standalone ELF programs, and a directory hierarchy modeled on conventional Unix layout, with dedicated locations for user data, system binaries, device access points, and configuration.

**Testing.** A multi-layered test suite spanning host-side unit tests, kernel self-tests that run inside the booted system, fuzz testing of input-parsing code paths, and a continuous integration pipeline that runs on every change.

## Project layout

```
kernel/     core boot sequence, process management, syscalls, security
mm/         physical and virtual memory management
fs/         file system and encryption layer
drivers/    hardware drivers (disk, keyboard, display, clock)
arch/       architecture-specific boot and CPU setup code
apps/       user-space programs, including the shell
tests/      host-side and in-kernel test suites
tools/      host-side utilities used by the build process
```

## Requirements

Building and running the kernel requires a GNU toolchain targeting x86 (gcc, nasm, ld), QEMU, grub-mkrescue, and OpenSSL development headers for the build-time tooling.

## Current status

The project is under active development. The kernel boots, manages memory and processes, and runs a functional shell with a working file system, user authentication, and basic device access. Ongoing work is focused on hardening the security model, closing gaps in test coverage, and preparing the codebase for eventual deployment to real hardware.

## Building and running

The kernel is built with a standard GNU toolchain and tested in QEMU. A Makefile drives the full build, disk image preparation, and test execution.

```
make run          # build the kernel, prepare a disk image, and boot it in QEMU
make test          # run the host-side unit tests
make test_kernel   # boot the kernel in a self-test mode and run its internal test suite
make fuzz          # run the fuzzing harness against input-parsing code
```

## Direction

The next phase of the project focuses on extending the file system and security model, expanding the set of available user-space programs, and beginning the work required to run outside of an emulator, on real hardware.
