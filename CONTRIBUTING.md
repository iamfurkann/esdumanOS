# Contributing to esdumanOS

Thank you for your interest in contributing to esdumanOS! This document provides guidelines for contributing to the project.

## Getting Started

1. **Fork** the repository and create a feature branch from `main`.
2. Set up the build environment using the dependency list in the [README](README.md#building).
3. Run the full test suite before submitting changes:
   ```bash
   make test && make fuzz && make && make test_kernel && make test_smap
   ```
   If your change touches entropy, crypto, or the `/dev` random devices, also run the
   configuration that exposes RDRAND — it reaches a branch the other targets cannot:
   ```bash
   make test_kernel QEMU_TEST_CPU="-cpu qemu32,+rdrand"
   ```

## Development Environment

### Requirements

- GCC 9+ (with multilib support)
- NASM 2.14+
- GNU Make 4.0+
- QEMU (`qemu-system-i386`)
- grub-mkrescue, xorriso, mtools
- OpenSSL development libraries
- Python 3.6+

### Building

`ESDUMAN_ELF_KEY_HEX` must be set and must be **exactly 64 hexadecimal characters** —
the 32-byte AES-256 key used to encrypt the embedded ELF binaries. The Makefile checks
both the presence and the length and aborts otherwise, so a passphrase-style value will
not work.

```bash
export ESDUMAN_ELF_KEY_HEX=$(openssl rand -hex 32)
make clean
make
```

The key is compiled into the kernel image, so treat it as a build parameter rather than
a secret.

## Code Style

- **Language:** GNU C, freestanding (no libc). The build passes no `-std`, so it takes
  the compiler default and relies on GNU extensions throughout — inline `asm`,
  `__attribute__`, statement expressions. It does not compile under strict ISO mode and
  is not meant to. Assembly in NASM syntax.
- **Indentation:** Tabs for indentation, spaces for alignment.
- **Naming:** `snake_case` for functions and variables. `UPPER_SNAKE_CASE` for macros and constants.
- **Comments:** Comment non-obvious logic. English is preferred for new contributions.
- **Headers:** Include the minimal set of headers required. Avoid pulling in `kernel.h` when a specific subsystem header suffices.

## Commit Messages

Use concise, descriptive commit messages with subsystem prefixes:

```
mm: fix backward coalescing in kfree()
fs: add directory traversal to VFS
proc: implement signal delivery for user processes
drivers: fix PS/2 keyboard AltGr handling
crypto: optimize AES-256 key schedule
tests: add pipe EOF detection test
build: update CI to use checkout v4
```

Valid prefixes: `mm:`, `fs:`, `proc:`, `drivers:`, `crypto:`, `tests:`, `build:`, `docs:`, `security:`, `arch:`, `lib:`, `apps:`

## Pull Request Process

1. Ensure all tests pass (`make test && make test_kernel && make test_smap`). CI runs
   the same targets and none of them are allowed to fail.
2. Add or update tests for any new functionality.
3. Update documentation if the change affects user-visible behavior.
4. One feature or fix per pull request.
5. Provide a clear description of what your PR does and why.

## Areas Where Help Is Needed

We especially welcome contributions in these areas:

- 🧪 **Testing** — Expanding the test suite, especially edge cases
- 📝 **Documentation** — Improving code comments and README sections
- 🖥️ **User-space programs** — Writing new programs for `/bin`
- 🌐 **Network stack** — TCP/IP implementation
- 🔧 **Hardware testing** — Testing on real hardware and reporting results
- 🏗️ **Architecture** — Improving memory management, adding fork/wait

## Reporting Bugs

- Use the [GitHub Issues](https://github.com/iamfurkann/esdumanOS/issues) tracker.
- Include steps to reproduce, expected vs. actual behavior, and your environment.
- If applicable, attach serial output or kernel log (`dmesg`).

## Code of Conduct

This project adheres to the [Contributor Covenant Code of Conduct](CODE_OF_CONDUCT.md). By participating, you are expected to uphold this code.

## License

By contributing to esdumanOS, you agree that your contributions will be licensed under the [MIT License](LICENSE).
