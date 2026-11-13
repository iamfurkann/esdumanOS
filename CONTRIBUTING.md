# Contributing to esdumanOS

Start from current `main` and keep each change reviewable. Use the Linux
[build environment](README.md#building) and read [SECURITY.md](SECURITY.md)
before reporting a vulnerability publicly.

## Getting Started

Fork or create a feature branch, install the complete README dependencies,
and export a build key before establishing a baseline:

```bash
export ESDUMAN_ELF_KEY_HEX=$(openssl rand -hex 32)
make -B BUILD=prod all
```

The key must be exactly 64 hexadecimal characters. Keep it stable during
incremental builds: make does not track environment changes. It is embedded
in the kernel to package ELF assets, not a login/disk password or a signing
trust root. The maintained CI host is Ubuntu 24.04 x86; local validation also
uses a Debian 13 x86 VM.

For the Mac/VM workflow, edit on the Mac and build/run executable tests on the
Linux VM. Transfer explicit file lists without deleting remote files. Check
for remote edits before overwriting and compare independent hashes. Exclude
credentials, populated images and private instruction/handoff files.

## Validation

These are the eight executable stages in [CI](.github/workflows/ci.yml):

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

No CI stage is allowed to fail. RDRAND is part of the matrix, not an optional
replacement for SMAP. BIOS, q35, UEFI and RDRAND run 46 kernel-mode modules
plus Ring 3 with matching totals. SMEP/SMAP runs Ring 3 only and intentionally
has a smaller total. UEFI boots GRUB from an ISO; other kernel targets use
QEMU's direct kernel loader.

Every kernel target has an empty xHCI decoy and an active controller with
keyboard/mouse and two USB disks of different capacities on explicit root
ports. Tests use disposable images under `TESTRUN_ROOT`, not interactive ones.
For iteration:

```bash
make test_kernel MODULE=mount,vfs,bcache,ring3
make test_matrix QEMU_MATRIX_TIMEOUT=5400
```

`test_matrix` includes BIOS, q35, UEFI and SMAP; it excludes RDRAND, host tests,
fuzzing and production CLI acceptance. A filtered run proves only its selected
modules. Simultaneous invocations need separate `TESTRUN_ROOT` directories.
Nested emulation may need longer observation deadlines; record timeouts and
do not reduce KDF costs or remove assertions to make a gate pass.

Storage/boot changes also need [production CLI acceptance](README.md#production-storage-acceptance)
and, where durability is affected, `make test_powercut`. Both are outside CI.
Use normal-KDF production fixtures, not test/dev disks. Retain completed
reports, terminal/serial logs and exact binary hashes. Partial progress and
timeouts are not passes; distinguish QEMU from physical-machine evidence.

For documentation-only changes, check examples, links, anchors and claims
against source; do not claim an executable suite was rerun. For code changes,
add a regression that fails with the original defect and run affected tests,
broadening coverage where warranted. Before release, follow
[RELEASING.md](docs/RELEASING.md).

`make clean` preserves interactive images but deletes `build/` and its
validation evidence. Retain needed logs elsewhere. `make fuzz` may add corpus
files; review generated inputs before staging them.

## Code Style

- Use freestanding GNU C and NASM x86 assembly. The build uses GNU extensions
  and the compiler's default GNU dialect, not strict ISO C.
- Follow the surrounding file's indentation; existing files use tabs and
  spaces. Avoid unrelated reformatting.
- Use `snake_case` for functions/variables and `UPPER_SNAKE_CASE` for macros.
- Explain non-obvious ownership, bounds, failure paths and hardware ordering
  in English comments. Describe limits without unsupported guarantees.
- Prefer specific subsystem headers to importing everything via `kernel.h`.
- Preserve assigned syscall numbers and retired holes. Changed semantics,
  shared structures or disk layouts require compatibility notes and tests.

## Commit Messages

Use a concise English subject. Subsystem prefixes (`fs:`, `drivers:`, `docs:`)
and conventional forms (`fix(fs):`) are useful guidance, not an enforced list.

```text
drivers: reject HID usages outside the translation table

Keyboard reports can carry a full byte, while the table ends at 0xe7.
Reject the remaining values before indexing and exercise press/release
reports followed by a valid key.
```

Separate subject and body with a blank line. Write flowing prose around 76
columns describing the trigger, behavior and evidence. Split independent
changes into separate commits while keeping each coherent.

## Pull Requests

Use the [template](.github/pull_request_template.md), select applicable types
and subsystems, and mark `[x]` only for completed work. Name test commands,
configurations and results; distinguish automated terminal/QMP runs from
manual interaction. Include compatibility and upgrade steps. Remote CI must
pass before merge; local test records are not remote CI results.

## Useful Contributions

Priorities include power-loss recovery and versioned migration, storage fault
injection, bounded device/firmware parsing, reproducible hardware validation
and diagnosis of long unlock observations. COW `fork`/`wait` already exists;
lifecycle and failure-path coverage can improve it. Small user programs and
documentation improvements are welcome. Networking, SMP and new architectures
need separately scoped designs.

## Reporting Bugs

Use [GitHub Issues](https://github.com/iamfurkann/esdumanOS/issues) for ordinary
bugs. Include the source commit/local changes, build flavor, exact QEMU
command/firmware/CPU, storage topology, reproduction and final verdict or last
stage. Redact credentials and passwords; do not upload populated disk images.
Use the private route in [SECURITY.md](SECURITY.md) for vulnerabilities.

## Conduct and License

Participation follows [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md). Contributions
are licensed under the project's [MIT License](LICENSE).
