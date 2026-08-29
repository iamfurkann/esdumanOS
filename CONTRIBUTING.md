# Contributing to esdumanOS

Thank you for your interest in contributing to esdumanOS! This document provides guidelines for contributing to the project.

## Getting Started

1. **Fork** the repository and create a feature branch from `main`.
2. Set up the build environment using the dependency list in the [README](README.md#building).
3. Run the full test suite before submitting changes:
   ```bash
   make test && make fuzz && make && make test_kernel && make test_kernel_q35 && make test_kernel_uefi && make test_smap
   ```
   `test_kernel_q35` runs the same suite on a machine with no IDE controller, where the
   disk is behind a SATA controller. `test_kernel_uefi` runs it again on the boot path a
   real machine uses — an ISO, GRUB, UEFI firmware and a framebuffer console — and it is
   the only target that does not pass `-kernel`, so it is the only one in which a
   bootloader runs at all.

   All three must report the same assertion total; a different one means an assertion has
   learned which machine it is running on.

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
- QEMU (`qemu-system-i386`, plus `qemu-system-x86_64` for the UEFI test target)
- grub-mkrescue, xorriso, mtools
- `grub-efi-amd64-bin`, without which `grub-mkrescue` still succeeds and produces an ISO
  that boots on BIOS only
- `ovmf`, the UEFI firmware `make test_kernel_uefi` boots under
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
a secret. It decrypts the embedded programs and nothing else: since 1.1.0 the file
system is encrypted under a key unwrapped from a passphrase at boot, and the embedded
images are re-encrypted under that key as they are written to disk.

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

The prefix names whatever the change is about — a subsystem, a driver, a single
program. `mm:`, `fs:`, `proc:`, `drivers:`, `crypto:`, `tests:`, `build:`, `docs:`,
`security:`, `arch:`, `lib:` and `apps:` are the usual ones, and the history also
carries `kernel:`, `vfs:`, `syscall:`, `abi:`, `sh:`, `edit:`, `klog:`, `tty:`, `rtc:`,
`entropy:` and the names of individual tools. Pick the narrowest thing that is true.

This line used to call those twelve the *valid* prefixes, which described a rule the
project has never followed: of the last forty commits, three matched the list. A closed
list nothing enforces and nobody follows is worse than no list, because it makes a
reader think they have broken a rule when they have not.

The body matters more than the prefix. Subject line, blank line, then flowing prose
saying what was found, why it went unnoticed, and what was deliberately left alone.
Prose rather than bullets, wrapped at about 76 columns.

## Pull Request Process

1. Ensure all tests pass. The list is the one under [Getting Started](#getting-started)
   — `make test`, `make fuzz`, `make`, `make test_kernel`, `make test_kernel_q35`,
   `make test_kernel_uefi`, `make test_smap` — and CI runs every one of them plus
   `make test_kernel QEMU_TEST_CPU="-cpu qemu32,+rdrand"`. None of them is allowed to
   fail.

   This said "`make test && make test_kernel && make test_smap`" and "CI runs the same
   targets", which was three of the six. A contributor who trusted it would pass
   locally and then watch `make fuzz` or the RDRAND run fail on the pull request,
   having done nothing wrong.
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
