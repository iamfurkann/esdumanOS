# Reliability audit

Original audit: 2026-10-24. Latest documentation/HID review: 2026-11-13
(Europe/Istanbul).

## Documentation reconciliation and HID boundary validation (2026-11-13)

This is the latest source handoff. Earlier sections retain their own snapshot
and binary identities; they are not all results for the ISO named here.
The base remains `f39ba47c4c658130ea4c890c4824e0dd99237fac` on Mac and VM.
No staging, commit, branch, pull, push, tag, merge or publication was performed.

### Documentation scope

All ten public Markdown documents and LICENSE were read, including the full
historical changelog, contribution/security policies, issue/PR templates and
this audit. CODE_OF_CONDUCT and LICENSE needed no changes. README was reconciled
with the boot sequence, memory map, syscall/command inventory, v3 compatibility,
provisioning and test targets. SECURITY now states the actual keyslot HMAC
fields, metadata/replay boundaries and entropy/persistence limits. CONTRIBUTING
and templates distinguish complete CI, targeted tests and automated terminal
acceptance. `docs/RELEASING.md` adds build, asset, checksum and publication steps.
Historical changelog entries retain their historical wording with an explicit
notice directing current claims to README/SECURITY.

Ten C/header files have documentation-only deltas after the tested source
snapshot. A lexer-aware comment comparison retained string/character literals
and verified identical non-comment text; test diagnostic line numbers can
move. The Makefile's reset-disk help text now requires explicit provisioning.
The VM image-target fixture passed again. No KDF, assertion, deadline or runtime
security policy was relaxed as part of the documentation changes.

### HID defect exposed by the audit

`usbkbd_scancode_for()` indexed a 232-byte table with an unrestricted byte.
The old security text incorrectly treated `uint8_t` as a sufficient bound.
Usages 0xE8–0xFF now return zero before indexing. Regression coverage exercises
all 24 values through direct lookup and press/release reports, then valid input.
A VM ASan/UBSan harness against the real driver passed with the guard (exit 0);
removing only the guard produced `global-buffer-overflow` (exit 1).
Evidence: `documentation-usbkbd-5u_h_c6d` in the VM validation directory.

### Current validation

| Check | Observed result |
| --- | --- |
| Host suite | Exit 0 |
| BIOS, q35 and UEFI full kernel suites | 1685 passed / 0 failed each |
| RDRAND full kernel suite | 1685 passed / 0 failed |
| SMEP/SMAP Ring 3 suite | 288 passed / 0 failed |
| Public docs | 52 local links, 17 shell blocks, 68 syscall entries and 46 module entries checked; no errors |
| Current production UEFI | Login, independent secondary unlock, both payload reads, sync, ACPI halt and offline metadata checks passed |
| Current production BIOS | Passed on a fresh copy in an isolated repeat |
| ISO inspection | Extracted payload matches, Multiboot 1/2 and BIOS/UEFI catalog valid; test entry points and reduced-KDF build flag absent |

Kernel/CPU gates: `documentation-usbkbd-5u_h_c6d/result.json` (exit 0).
Current test kernel SHA-256: `8c55a38f30fbca6a7ed612c6dbb9124b41dd6e720c9f1b138dfc5a8372380fad`.
Production ISO SHA-256: `fabbda2a676c56ef79bb373ed264a4b8d486d0483b5f7b8cd717e2cf3d1fa49c`.
Production kernel SHA-256: `ddb1623d6a78f1fd352e7916062a3d38fcf02531871f1cb28ea9447dfc55399a`.
The ISO is 22913024 bytes. Its independently measured Mac and VM MD5 hashes
also match. These identify an uncommitted-source build, not a future tag build.

Current production evidence is deliberately narrower than the earlier complete
provisioning/legacy-refresh scenario: it boots copies of the earlier accepted
root/data disks and verifies login, attach, reads, sync and halt with the new
HID-fixed binary. UEFI passed in `documentation-production-eb8kgid0/uefi`.
BIOS repeat evidence is `documentation-bios-repeat-n8r0yiyl/bios`.
Original fixture disks were never opened for writes by the new guests.
The full legacy-refresh, first-provisioning, power-cut and fuzz evidence below
remains evidence for unchanged implementation paths, not reruns on this ISO.

### Unresolved foreground-job observation

The first current-ISO BIOS run logged `Disk passphrase: [1] stopped` during
`mount usb0`, then returned to the shell. Subsequent automated test input was
interpreted as a shell command. The worker failed; it is not a successful
storage result. The private terminal transcript and failed result remain in
`documentation-production-eb8kgid0/bios`. UEFI in the same pair passed.
The isolated BIOS repeat passed without source, KDF or deadline changes. That does not explain or fix the first failure.
The command's foreground/job-control timing warrants separate investigation;
this documentation task does not claim a fix. A returned shell prompt is not
a password prompt. Earlier 900-second unlock timeouts below are a separate
unresolved observation and remain recorded too.

### Delivery and remaining publication work

The final plan covers 58 unique source paths in 36 separate commit groups.
Independent Mac/VM hashes are recorded in `documentation-final-md5.json` in
the local delivery folder. PR/release Markdown and Git steps are local delivery
artifacts; they were not sent to the VM. Private handoff/instruction files,
credentials and populated disks are excluded from the public package.
The maintainer still owns review, remote CI, commits, merge, tag and publication.
The filesystem remains nonjournaled; no arbitrary-power-loss recovery or
physical-hardware validation is claimed.

## Earlier v1.14.0-beta.1 handoff before the HID fix (2026-11-13)

This section supersedes the current-state claims below for the uncommitted
v1.14 work. The dated v1.13 audit remains historical evidence. The base is
`f39ba47c4c658130ea4c890c4824e0dd99237fac` on local and VM `main`.
Source and QEMU acceptance are complete. Commits, remote CI, merge, signed tag
and publication remain user-owned; no real-hardware certification is claimed.

### Scope and acceptance evidence

The release must keep the boot filesystem live while a second, independently
keyed v3 filesystem is attached at `/mnt`. Runtime directory identities include
a mount generation. The 96-byte disk entry and its 16-bit identifiers remain
unchanged. Syscall 73 is the explicit `MKFS` operation. Calls 71 and 72 keep
their numbers, with changed attach/detach semantics documented in the ABI.

| Requirement | Current evidence | Remaining gate |
| --- | --- | --- |
| Independent metadata, keys and device routing | Final kernel matrix and complete BIOS/UEFI production persistence passed | None within the tested QEMU scope |
| FD/CWD lifetime and stale IDs | Kernel/Ring 3 ownership and production BIOS/UEFI busy-CWD checks passed | None for these checks |
| Explicit provisioning | Kernel refusal cases, production first boot and BIOS/UEFI mkfs CLI passed | None for these checks |
| Mount never formats | Kernel refusal cases and production blank-media no-write checks passed | None for these checks |
| Buffered metadata failures | Fault-injected admission retry and failed-detach retention passed | Power loss remains nontransactional, as measured below |
| Preserve interactive images | VM host fixture passed; destructive-clean negative control failed as expected | None for this fixture |
| Compatibility and release tests | Host, kernel/CPU matrix, fuzz, six power cuts, BIOS CLI and full UEFI legacy refresh/persistence/shutdown passed | User's final release and remote CI checks |
| Release materials | 24 commit messages covering 44 unique files, Git steps, PR/release Markdown and work report prepared | User performs Git and publication |

### Discovered During Implementation

- P0, `fs/vfs.c` table I/O copied full 512-byte sectors into an allocation
  sized to the exact FAT entry count. A 479-entry FAT occupies 1916 bytes,
  but four sector reads wrote 2048. Bounded scratch-sector copies now handle
  both reads and zero-padded writes. Tail and geometry regressions passed in
  the targeted and full kernel runs recorded below.
- P0, metadata writers discarded cache-admission failures. They now return
  errors and retain pending tables for sync/detach retry. No journal or
  power-loss transaction guarantee follows from that change. The read-only
  `tools/check_fs_image.py` now checks v3 geometry, names, parent chains,
  allocation lengths, ownership and orphan clusters. It never decrypts,
  repairs or writes the image. Its VM regression tests and reproducible
  power-cut evidence remain separate acceptance gates.
- P0, `fs_atomic_update` removed a pre-existing `<name>.tmp`. It now chooses
  an unused temporary name while holding the VFS lock. The preservation
  regression passed in the targeted and full kernel runs.
- P0, mounted names were passed to string routines without a bounded NUL
  check. The mount parser now rejects invalid names and reserved slot zero.
  FAT validation rejects shared, cyclic, short and orphaned chains, and checks
  their lengths before the candidate can enter the namespace. Corrupted RAM
  fixtures passed each rejection without target writes in the kernel matrix.
- P0, resolving a path containing `private/../file` checked only the final
  ancestor chain and could skip search permission on `private`. Resolution
  now checks each traversed component. An unprivileged fork test covers both
  open and chdir through that path on the secondary volume. Both passed in
  the targeted run and the full kernel matrix.
- P1, boot installs embedded user programs only when `/init.elf` is absent.
  An existing v3 root therefore retains its old `sh`, `mount` and `umount`,
  which cannot use the new unlock protocol or `mkfs` builtin. The disk format
  is readable, but CLI compatibility needs an explicit, backed-up program
  refresh procedure and a legacy-image test. README now specifies renaming
  the changed programs, init and version text to `.pre-v114` backups, syncing
  and rebooting into the new image's missing-program installer. A production
  ISO harness exercises this on copies. No automatic overwrite of user
  programs has been added. The final UEFI run `uefi-myaz433_` passed the
  old-version/payload checks, refresh, subsequent CLI operations, reopening both
  volume payloads after reboot and ACPI shutdown.
- P1, the shell password reader silently truncated input, allowing a new
  `mkfs` passphrase longer than 63 characters to become a different secret.
  It now drains the line and rejects overflow. The early boot reader also
  returns an empty, rejected input on overflow. The production CLI harness
  exercises 64-character rejection at disk setup and before `mkfs` initializes
  the blank data disk.
- P1, `apps/init.c` accepted only 31 password characters, while first-boot
  setup accepts 63. Such an account could be created but could not log in.
  Login now accepts the same 63-character ceiling and rejects an overlong
  line instead of authenticating its truncated prefix. The production harness
  has a `--long-password` regression for successful 63-character login and
  refusal of a 64-character input with that same valid prefix. The production
  first-boot transcript and screenshot in `bios-n2igiahy` confirm both outcomes.
- P1, `AUTH` removed trailing spaces even though account setup includes them
  in the password hash. A correctly created password ending in a space could
  never authenticate. AUTH now preserves spaces, matching setup and SETUID.
  A child-process Ring 3 regression accepts the exact test password and rejects
  an appended space without contaminating the parent's rate-limit state. The
  production 63-character fixture now ends in a space to cover positive login.
  The new assertion passed in the SMEP/SMAP run (288/0), and the production
  first-boot login accepted this exact 63-character fixture. The older 1678
  full-suite totals exclude the new assertion.
- Test gap, the production harness watched only serial output, but
  `SYSCALL_WRITE` sends application output to the terminal cells. It therefore
  could not observe `login:`. The harness now reads those cells through QMP's
  read-only memory dump, using addresses from the exact production binary and
  validating the i386 terminal object sizes. It records terminal transcripts
  and captures a login screenshot without patching the guest kernel.
- Test gap, sampling those cells at the shell's temporary leftward cursor
  position replayed an earlier prompt as new output. The same problem affected
  scrollback when its oldest row was discarded. Both production runs reached
  login but then stopped with a false command-status mismatch. The transcript
  now ignores cursor-only redraw snapshots and matches retained history across
  scrolling. Synthetic terminal snapshots cover both observed failure modes.
- P1, `fs_commit_writes` returned before freeing its buffer when a volume
  identity could not be resolved. All dirty completion paths now release the
  buffer, including stale-volume errors. The no-op path accepts a clean file.
- Test gap, manual `vfs_file_t` fixtures did not initialize the added mount
  field. They are zero-initialized. The initial provisioning run reported
  **201 passed / 2 failed** in these write-buffer assertions, not a clean gate.
- Test gap, `test_two_volumes` was defined but not called. Its call is now in
  the Ring 3 payload. Any earlier run cannot support the Ring 3 mount claims.

- P1, the timer RTC callback could interrupt a CMOS index/data port pair,
  selecting another register before the original access. A host fixture compiled
  from the real driver reproduced six failures: register read/write interference
  and twelve-hour midnight/noon decoding. IRQ-state-preserving access and a
  protected SET/write/restore transaction reduce that fixture to zero failures.
  All 24 hours in four register modes and nested interrupt restoration are
  covered. The original kernel assertion is retained with diagnostics; 64
  interrupt-enabled round trips were added. BIOS, q35 and UEFI time runs each
  passed 66/0 (`rtc-target.log`). This covers the single-CPU IRQ reader; the
  kernel has no CMOS-reading NMI handler or SMP support.
- P1, `xhci_init` armed timer polling before `usbmsc_init` issued synchronous
  configuration commands. The poller could consume their completion and cause
  a timeout. In `final-three-firmware.log`, BIOS lost the first disk after
  `Configure Endpoint never completed`, resulting in four USBMSC failures.
  Commands and EP0 transfers now own the ring before their doorbell, match the
  queued TRB pointers (plus slot/endpoint for EP0), and dispatch unrelated
  keyboard and bulk events while waiting. IRQs remain enabled for the PIT
  deadline. A test hook forces the real poller when a completion is pending.
  BIOS/q35/UEFI targeted xHCI/USBMSC runs each passed 75/0. A separate mutation
  binary removing only the poll guard failed 11 assertions (64/11, QEMU
  debug-exit 35), confirming that the regression detects the lost completion.
  Evidence: `xhci-negative-2_8v747a/report.json`.
- Test gap, production runs used a 15-second socket.makefile deadline for QMP.
  BIOS reached sync/reboot and UEFI completed the backed-up v1.13 program
  refresh, but both later stopped on a QMP read timeout. These are incomplete
  harness runs, not passing persistence results. Framed reads now retain a
  delayed partial response for a bounded 120-second total request deadline,
  correlate request IDs, and never retransmit a command. Four transport tests
  cover delayed fragments, deadline, disconnection/mismatched IDs and errors;
  five terminal tests cover cursor, scrollback and login-screen transitions.

- Test gap, the production UEFI harness set only q35's host PCI-hole size.
  OVMF still placed xHCI above 4 GB; the 32-bit driver correctly refused it.
  `uefi-2kou8ulf/first.serial.log` records the inaccessible BAR and missing USB
  device. That run was stopped before claiming storage success. The harness
  now uses the same `X-PciMmio64Mb=0` firmware setting as `test_kernel_uefi`,
  rejects missing USB topology early, and requires a positive usb0/4096-sector
  registration for blank-boot and login evidence. This does not add PAE or
  relocate firmware-assigned BARs in the kernel.

### VM observations

Final kernel validation completed on the RTC/xHCI-corrected binary:
BIOS, q35 and UEFI each passed **1682/0**; RDRAND passed **1682/0** and
SMEP/SMAP passed **288/0**. `final-detached-4vkl3n7s/matrix-result.json` and
`cpu-result.json` both record exit 0; their companion logs retain the verdicts.
The test kernel SHA-256 is
`3cd603380454c606721242990891d298be45f3ae1119cc0d99568d3e006c43c9`.
An explicit compiler barrier was added at poll-owner release during review;
rebuilding both xHCI object variants produced byte-identical objects, so the
running binary's code was unchanged (`release-order-report.json`).

The current `make test` also passed, including six metadata test methods, nine
terminal/QMP cases and the deterministic RTC driver model
(`production-pair-684cyqwt/host.log`, exit 0). Earlier targeted RTC runs passed
66/0 on each firmware; targeted xHCI/USBMSC runs passed 75/0 on each. The
pre-fix full run in `final-three-firmware.log` reported BIOS 1675/4, q35 1679/0
and UEFI 1678/1. These failures motivated the driver fixes above. A subsequent
`drivers-final-matrix.log` attempt was interrupted by a VM restart and is not
counted as a pass.

The rebuilt production ISO passed extraction, both Multiboot headers,
BIOS/UEFI boot catalog and production-symbol checks
(`iso-drivers-audit-5yvcxgf1/report.json`). ISO SHA-256:
`82ae2518ff1e5c6225b53265174e829c8e8a13dde8b84cf99d3168c8c247bafa`.
Its kernel SHA-256 is
`f0c38accacd490b3c5390a26ddb576e65b0d23b3387db4573ed18a68540b611f`.

Production BIOS acceptance completed in `production/bios-ewbkkyab/report.json`
with worker exit 0 (`production-pair-684cyqwt/bios-result.json`). The final ISO
accepted the 63-character account password ending in a space, rejected its
64-character extension, preserved blank media on refused operations, explicitly
initialized the data disk and exercised two independent volumes. Busy CWD
refusal, detach/remount, sync/reboot, both reopened payloads and ACPI poweroff
passed. Offline metadata checks found 46 root entries/99 allocated clusters and
one data entry/one cluster. This BIOS seed was already v1.14, so it does not
establish legacy program refresh.

Production UEFI acceptance also completed with exit 0 in
`production-uefi-observed-0co1skt6/result.json`; the full CLI report is
`production/uefi-myaz433_/report.json` beneath that directory. The seed hash
matches the preserved v1.13 root (`f5bbe1bba5649a1a4f0646cfd2af0998d6c175bf28f30697cafd34c98ce17d18`).
The original provisioning directory retains the unmodified legacy kernel,
SHA-256 `b74e78092058e634388668cb616651a849ae6334f84b657b0455cb7ee8f6ae6c`.
The final ISO preserved the old user payload, refreshed all six backed-up
program/text paths, passed the two-volume operations and read `volumeproof14`
and `rootproof14` after a fresh boot before powering off through ACPI.
Root metadata has 52 entries/123 allocated clusters, including the backups;
the data volume has one entry/one cluster. Both metadata checks passed.

The observer retained the original harness and its 900-second waits. It sampled
CPU state, the PIT counter and the pinned binary's PBKDF2 loop through read-only
QMP memory reads; it did not call guest functions or modify guest memory. All
waits passed. The three root unlock waits took about 207, 131 and 274 seconds;
the longest individual wait was about 328 seconds for explicit mkfs. The final
BIOS and UEFI reports both name the ISO hash above. No completed worker or QEMU
process remains live.

An earlier run, `production/uefi-n96vld_m`, verified
the v1.13 seed and existing payload, completed all six program/text backups and
refresh, explicit mkfs, wrong-password refusal, two-volume I/O, busy CWD,
detach/remount and sync/reboot. Its second boot then exceeded the 900-second
disk-unlock deadline. The worker exited 1; there is no passing final report.
The serial log ends with the newline emitted after submitting the passphrase.

A separate diagnostic on copies of that UEFI root and data disk completed
unlock successfully in approximately 245 seconds after typing. All five input
bytes were consumed; samples showed SHA-256 execution with interrupts enabled
and an advancing timer. It used the same production kernel and the unchanged
600000-iteration keyslot (`uefi-unlock-diagnostic-xnf7pi62/report.json`). This
does not establish the cause of the earlier timeout or itself verify post-reboot
file contents. The complete observed production rerun above supplies that
separate persistence and shutdown evidence. Earlier unlock timeouts remain
recorded; their exact cause has not been established or claimed fixed.

Earlier BIOS `bios-ueh2gsir` completed
through two-volume I/O, busy-CWD refusal, detach/remount, sync and reboot, but
its second boot exceeded the 900-second unlock observation deadline. UEFI
`uefi-njg3q1g0` verified the old version and user payload, renamed all six
program/text paths to backups and rebooted, then exceeded the same deadline.
Neither is a passing full report. Their metadata checks pass and each root's
superblock sector is byte-identical to its original seed, including the
600000-iteration key slot. A copy of the saved BIOS root unlocked successfully
in an isolated diagnostic run: all five input bytes were consumed, samples
showed SHA-256 execution with interrupts enabled, then user mode. Evidence:
`unlock-diagnostic-l8d8fj3w/report.json`. The earlier timeout's exact cause is
not established. Neither the KDF nor assertions nor observation deadlines
were weakened for these diagnostics or the observed UEFI rerun.

Compilation and executable tests run only in `/home/esduman/kernel` over
`ssh -p 2222 esduman@localhost`. Editing and independent MD5 calculation are
on the Mac. `HANDOFF.md` and the private instruction document are excluded.

The chronological entries below retain intermediate failures and actions as
recorded at those checkpoints. Their pending work was superseded by the final
kernel/host results and current production status above.

- `make test_kernel MODULE=mount,vfs,bcache` on the intermediate provisioning
  snapshot finished with 201 passed and 2 failed. Durable VM log is
  `/home/esduman/v1.14.0-validation/provision.log`.
- `make -j2 test_kernel MODULE=abi,mount,vfs,bcache,ring3
  TESTRUN_ROOT=build/v114-target` reached secondary unlock and then the
  600-second harness deadline. It has no final assertion verdict. Durable
  log is `/home/esduman/v1.14.0-validation/multimount.log`.
- Both processes were confirmed absent after the VM reboot. A new run must
  use the current source manifest and a longer observation deadline. The KDF
  iteration count and assertions are retained.
- The `buffer-and-ring3` manifest matched 32/32 files by independent MD5,
  with differing-file and empty-hash negative controls rejected. Running
  `make -j2 test_kernel MODULE=abi,mount,vfs,bcache,ring3
  TESTRUN_ROOT=build/v114-buffer QEMU_TEST_TIMEOUT=1800` then completed with
  **627 passed / 0 failed**. Log is `buffer-and-ring3.log` in the same VM
  validation directory. Later parser, permission and child-ownership changes
  are not covered by this result.
- The first `make test` attempt stopped in the new disposable image fixture
  because Makefile version parsing needed `include/kernel.h`. The fixture now
  copies that header into its temporary directory. A rerun is required.
- The `namespace-validation` transfer again matched 32/32 files. The same
  targeted command with `TESTRUN_ROOT=build/v114-namespace` completed with
  **649 passed / 0 failed**. It covers corrupted RAM images, independent keys
  at identical local inode slots, metadata admission failure/retry, forked
  FD/CWD ownership, and unprivileged traversal. The subsequent duplicate-name
  check and host image checker require the next gate.
- `make test` on that snapshot passed, including the repaired image fixture.
  Log is `/home/esduman/v1.14.0-validation/host-rerun.log`.
- The image-preservation negative control replaced `clean` only inside a
  disposable VM fixture with a recipe that deletes its images. The regression
  failed with the expected missing `disk.img`, proving that it detects loss.
  Log is `image-negative.log` in the VM validation directory.
- The image-checker host suite initially failed its geometry test because the
  injected cluster count first violated the FAT overlap check. The fixture now
  sets 48 clusters, which fits one FAT sector but exceeds the 47-cluster data
  capacity. This exercises the intended data-extent check. The later
  `host-checker-final.log` run passed this corrected fixture.
- `make -j2 test_matrix TESTRUN_ROOT=build/v114-matrix` completed against the
  `image-checker-matrix` transfer (34/34 matching files). BIOS, q35 and UEFI
  each reported **1678 passed / 0 failed**. SMAP reported **287 passed / 0
  failed** because it runs the Ring 3 portion. Log is `matrix.log`. No RTC
  failure occurred in this run; one passing matrix does not prove that the
  previously documented RTC flake is eliminated.
- The subsequent `powercut-probe` transfer matched 36/36 files. Changes after
  the matrix were the opt-in crash module/harness, checker fixture correction,
  test registration and documentation. Production filesystem code is unchanged.
- `make test` then passed, including six image-checker test methods with their
  corruption subcases. Log is `host-checker-final.log`.
- The ELF libFuzzer/AddressSanitizer run used the existing 58 seed files and
  wrote new inputs outside the source tree. It completed **113589 executions
  in 11 seconds**, exit 0. Log is `fuzz.log`.
- `make -B -j2 BUILD=prod all` completed with exit 0, producing
  `esdumanOS-v1.14.0-beta.1.iso` (11188 sectors). Booting this production ISO
  interactively remains a separate gate.
- The first production BIOS CLI attempt used the 100000-iteration test
  image. The production kernel correctly refused that keyslot because its
  minimum is 600000. The halted test QEMU was stopped after inspecting the
  explicit refusal. This is a fixture mismatch, not a passing production boot.
  No production KDF limit was lowered. The harness now provisions a fresh
  disk through the production first-boot prompts before booting the release ISO.
- The first production-strength provisioning attempt reached Ring 3 after
  account creation, then the VM shutdown stopped QEMU (signal 15 from PID 1).
  There was no final CLI verdict. Its serial-only observation error is described
  above; a corrected run is required.
- `make -j2 test_kernel QEMU_TEST_CPU="-cpu qemu32,+rdrand"
  QEMU_TEST_TIMEOUT=1800 TESTRUN_ROOT=build/v114-rdrand` completed with
  **1678 passed / 0 failed**. Log: `rdrand.log`. This snapshot predates the
  later interactive password-reader changes.
- A separate read-only archive of v1.13 commit `f39ba47` was built in
  `/home/esduman/v1.14.0-validation/v113-baseline.GxgPng`.
  `make -j2 test_kernel MODULE=abi,bcache QEMU_TEST_TIMEOUT=1200` reported
  **216 passed / 0 failed** (`v113-baseline.log`). Its reduced-KDF test disk
  does not by itself establish production upgrade compatibility.
- These are agent-run provisional results, not user-certified release totals.

### Production validation continuation (2026-11-11)

The `production-terminal`, `production-upgrade` and `production-status`
transfers each matched 38/38 source files, including the login-reader fix.
The latest snapshot was checked again after a VM restart; all 38 independent
VM hashes still matched the unchanged Mac files. The production binary was
rebuilt after the login change. No build or executable test ran on the Mac.

- The terminal-based first boot in `bios-4rbf4on4` rejected a 64-character
  account password and accepted the corresponding 63-character password.
  The captured screen shows both the refusal and the root shell. The host
  harness then used `echo status-1-$?`, but this shell expands `$?` only as a
  separate token. That observation error stopped the gate; it was corrected
  to `echo status-1 $?` with an explicit result check. The run is not reported
  as a complete production pass.
- `production-resume-bios.log` records default blank-disk refusal without
  writes, login, creation of `/upgrade-probe`, all six program/version backup
  renames, sync, reboot and login after the missing-program installer.
  The VM stopped before the remaining data-volume checks finished, so there
  is no final report. Its former `bios-l665_v07/root.img` was no longer
  present when the VM was inspected on November 11.
- `production-resume-uefi.log` records default UEFI blank-disk refusal without
  writes and successful provisioning, login, user-file creation, sync and
  reboot using the unmodified v1.13 production kernel. Its former fixture
  `uefi-ztmruieq/provisioned-seed.img` was also absent on November 11.
  The separately archived v1.13 production binary remains available outside
  the source tree. The new ISO's full legacy refresh gate is still pending.
- `final-host.log` passed. The following full-kernel processes were interrupted
  by VM shutdown, without a final verdict; those partial logs do not replace
  the earlier completed matrix. `final-kernel-nov11.log` is the new full run.

All image paths above are relative to `/home/esduman/kernel/build/v114-production`.
That build directory and the main production ISO/kernel were absent on
November 11. The first two restart attempts therefore stopped before boot
with a missing-ISO error; neither is a kernel failure or a passing gate.
Fresh production runs will store their artifacts outside `build/`, under
`/home/esduman/v1.14.0-validation/production`.
The harness now saves its source and completed stages in each evidence directory.
`--seed` always copies its input; `--expect-seed-version` checks the existing
version text and old user payload before program refresh. Overflow rejection
before mkfs is checked even when a previously refreshed seed is used. A final
`report.json`, not a partial `progress.json`, is required for acceptance.

The `exact-password` source transfer matched 38/38 files. Its production build
and host suite passed. The ISO audit extracted `/boot/myos.bin` and compared it
byte for byte with the production binary, checked Multiboot 1/2 headers and
BIOS/UEFI boot catalog entries, and confirmed the absence of test entry points
and reduced-KDF compiler flags. The ISO SHA-256 was
`57f8697798f636510a287b71b82fcb2f130b01b898b360a69ed2d23933324974`.

`exact-password-matrix.log` contains a completed SMEP/SMAP result of **288
passed / 0 failed**, including the exact-password assertion. Its full BIOS,
q35 and UEFI targets were interrupted when the VM stopped; they have no final
verdict. The older `final-kernel-nov11.log` reached Ring 3 but exceeded its
1800-second observation deadline. Its harness prints "KERNEL HUNG", but this
log establishes only a timeout while assertions were still progressing.

Both production first-boot runs reached login. The BIOS run rejected 64
characters and accepted the 63-character password ending in a space; its
login screenshot was inspected. The CLI status observation error described
above then stopped those runs. Their root images passed the offline metadata
checker. The legacy fixture was created by the separately archived v1.13
production binary, SHA-256
`b74e78092058e634388668cb616651a849ae6334f84b657b0455cb7ee8f6ae6c`.

The `terminal-observation` transfer matched **39/39** files. `make test` passed,
including four terminal snapshot regressions. Replacing the reader in memory
with its previous implementation made all four fail, as expected; source files
were unchanged by this negative control. The production scenarios now use
copies of the verified roots with `--expect-seed-version` to check the old
version text and pre-existing user payload before refresh. Full BIOS/q35/UEFI
regressions were restarted with `QEMU_TEST_TIMEOUT=5400`, retaining all
assertions and KDF work factors. Final reports remain required.

### Reproducible power-cut experiment

`make test_powercut` builds the test kernel and runs six independent scenarios
on new 2 MB images under a unique `build/testrun/powercut/run-*` directory.
The opt-in `crash-*` probe is absent from the production kernel and ordinary
module list. It first synchronizes a baseline, then wraps the root block-device
callbacks to stop after a chosen successful physical-sector write during create,
update, delete or rename. A sixth case stops only after successful sync.

The host sends SIGKILL after the stopped marker, keeps the original snapshot,
runs the read-only checker, then reopens a copy. Structurally invalid snapshots
must be refused without any image writes. Valid snapshots must decrypt every
surviving named payload as one complete old or new version. The synchronized
case additionally requires the new payload to survive reopening. Host JSON
reports distinguish invalid metadata from successful recovery and retain hashes.

This models loss of kernel RAM at sector boundaries on QEMU file-backed storage.
It does not model torn physical sectors, a host power outage, drive firmware
caches or hardware flush barriers. Mid-operation consistency is measured, not
promised.

The first run used `--cases synced create-fat --timeout 900` and retained
`build/v114-powercut/run-4yk8v3s9/report.json` on the VM. After sync, the checker
accepted the image and a new QEMU boot decrypted the complete new payload
(1 passed / 0 failed). Cutting after the create operation's FAT write left
orphan cluster 98. The checker rejected it, and the next boot refused it with
`No mountable filesystem found.` The rejected image's SHA-256 did not change.
This is an observed v3 power-loss consistency defect, not successful recovery.

A redo journal can retain intent and replay a partially published operation,
but needs reserved space, authenticated commit records, replay rules and a
versioned migration. Copy-on-write metadata needs a versioned root selector and
space for both generations. Ordering existing v3 writes can favor orphan space
over dangling references but cannot atomically publish a FAT chain and a
separate directory entry. The present beta uses error propagation, retry and
fail-closed validation. It does not silently repair or claim journal-level
recovery. A format/transaction project is still required before promising
continued writable operation after arbitrary power loss.

The other four cuts completed in `build/v114-powercut/run-8rajtgfo/report.json`
with the same pinned test-kernel SHA-256 recorded by the reports. Reopening
each valid image decrypted a complete surviving payload (1 passed / 0 failed).
The invalid delete snapshot was refused without writes, just like create-fat.

| Cut | Offline metadata | Reopen observation |
| --- | --- | --- |
| synced | Valid, 44 entries / 97 allocated clusters | Complete new payload |
| create-fat | Invalid: orphan cluster 98 | Refused; image hash unchanged |
| create-dir | Valid, 44 entries / 97 allocated clusters | Complete surviving payload |
| update-dir | Valid, 45 entries / 98 allocated clusters | Complete surviving payload |
| delete-dir | Invalid: orphan cluster 98 | Refused; image hash unchanged |
| rename-dir | Valid, 44 entries / 97 allocated clusters | Complete surviving payload |

## Historical v1.13 audit (2026-10-24)

The sections below describe the earlier revision and its then-current open
work. For the v1.14 source and validation status, use the section above.

## Revision and operational scope

- Local branch: `main`; HEAD: `2b1b813a70f416c66880c712240a586021b00f71`.
- GitHub's `main` resolved to the same commit through the connected GitHub API.
  The open issue/PR search returned no results on this date.
- Committed version: v1.12.0-beta.1. The existing working tree identifies itself
  as v1.13.0-beta.1 and contains 26 modified tracked files plus three new source
  files. These changes predate this audit and are preserved. This is not a
  clean-main baseline or a v1.13.0 release certification.
- Source editing: `/Users/esadfurkanduman/kernel__` on the Mac. Compilation and
  executable tests: `/home/esduman/kernel` on VM `esduman42` only.
- VM environment: Debian GNU/Linux 13 (trixie), Linux 6.12.101+deb13-amd64,
  GCC 14.2.0, QEMU 10.0.11.
- `esdumanvm:22` refused SSH. The user supplied the working connection
  `ssh -p 2222 esduman@localhost`. The directory and matching revision were
  verified before transfer. No alternative host or disk was guessed.
- `HANDOFF.md` and the instruction document remain private and outside every
  source transfer and proposed commit. No Git staging, commits, branches, pushes,
  tags, releases, pull or cleanup commands were performed.

## Requirement status

Statuses describe source evidence; runtime outcomes are recorded separately.
`Partial` does not mean a subsystem is certified.

| Work package | Status | Evidence and remaining acceptance work |
| --- | --- | --- |
| Discovery and baseline | Partial | README, CHANGELOG, SECURITY, CONTRIBUTING, Makefile, CI, current commit and relevant source paths inspected. Existing source state matched across machines. Test results below. |
| P0-A: cache/device I/O | Implemented; targeted and matrix storage assertions passed | Admission checks, retained failed writes, bounded eviction, per-device isolation and lifecycle error propagation in this package. VFS metadata error handling remains a separate P0 task. |
| P0-B: crash consistency | Missing | `fs_atomic_update()` swaps directory entries and deletes the temporary file; it does not establish an on-disk transaction. `save_directory_to_disk()` and `save_fat_to_disk()` do not propagate all write failures. No read-only image checker or power-cut matrix found in `tools/` or `tests/host/`. |
| P0-C: RTC determinism | Reproduced; unresolved | `rtc_read_utc()` compares repeated raw snapshots. `test_time.c` separates calendar arithmetic from hardware round trips; README still documents flakiness. The full q35 run reproduced the date round-trip failure; isolated repeats are listed below. No root cause or deterministic fix is claimed. |
| P1-A: safe boot and disk preparation | Partial; P0 data risk remains | BIOS/UEFI test targets and MB2/ACPI exist. `init_fs()` still formats a disk whose checked header region is zero and boot can try alternate devices. A separate explicit format operation, deterministic disk selection and combined USB image design remain required. Secure Boot is not implemented. |
| P1-B: driver recovery/observability | Partial | ATA/AHCI have timeout paths; existing uncommitted xHCI/early-framebuffer work improves diagnostics. USB MSC and xHCI still need fault-injection and BOT recovery validation; no real-hardware success is claimed. |
| P2: mount context/lifecycle | Partial | Cache keys include device identity; syscall mount transitions now stop on flush failure. VFS still uses global FAT/directory/superblock/partition state. CWD identities, open descriptors, rollback and concurrent mounts need a dedicated context design. |
| P3: security hardening | Partial | uaccess range checks, SMEP/SMAP and adversarial/ELF tests exist. Global uaccess state, no stack canaries and non-PAE memory semantics remain. Crypto keyslot/format changes require a separate threat model and migration tests. |
| P4: expansion | Deferred | No SMP, GUI, network or unrelated features added while storage risks remain. |
| Fast CI | Present; current run has an RTC failure | CI defines `make test`, `make fuzz`, `make`, kernel BIOS/q35/UEFI, RDRAND and SMAP targets. New tests join existing kernel modules, requiring no Makefile or ABI registration changes. |
| Nightly/deep tests | Partial | `make test_matrix` exists in the pre-existing tree. No scheduled crash-consistency or storage fault matrix found. |
| Release gate | Not performed | Full release publication and authoritative assertion totals belong to the user. This package does not change version fields or certify real hardware. |

## Selected P0 package: accepted data must survive device failure

### Evidence before the change

1. `bcache_write_sector()` accepted out-of-range LBAs and read-only writes as
   `E_OK`. Admission did not validate buffers or the slot's fixed 512-byte size.
2. `write_is_hopeless()` treated `E_INVAL`, `E_ROFS`, `E_NODEV` and `E_NXIO` as
   reasons to invalidate dirty data. These can come from the driver *after*
   admission. A disk that disappears or becomes write-protected does not make
   its accepted data disposable. A later flush could return success after the
   earlier flush had erased the only copy.
3. `bcache_get_lru_slot()` attempted only the oldest slot. If its write failed
   with `E_IO`, even 63 clean slots could not make a cache miss succeed.
4. `sys_sync()` discarded the flush result and returned `E_OK`; reboot/halt
   proceeded regardless. `sys_mount()` ignored both flush results before
   `init_fs()` reset the entire cache. `sys_umount()` checked only the second,
   device-specific flush even though `init_fs()` resets other devices' slots too.

### Decision, alternatives and compatibility

- Share pure `blockdev_check_read/write()` checks between direct I/O and cache
  admission. Reject invalid LBAs, null buffers and missing handlers before
  touching cache state. Cache admission additionally requires 512-byte sectors.
- Keep every accepted dirty sector after any write failure. Rejecting invalid
  requests up front removes the original justification for drop-on-error.
  Classifying driver errnos as proof that bytes can be lost was rejected.
- Reuse invalid slots first, then the oldest clean slot. If all are dirty, try
  each in LRU order at most once. Total exhaustion returns `E_IO` and leaves all
  bytes intact. A single failing victim no longer blocks other usable slots.
- Propagate global flush status through sync and stop reboot/halt/mount/unmount
  before hardware power operations or destructive cache reinitialization.
- No syscall numbers, structures, disk layout, keyslot or format version change.
  Existing callers can now receive the error that was previously hidden. Shell
  sync/reboot/halt already convert negative results into a failed exit status.
  No disk migration is needed.

### Guarantees and limits

`E_OK` on a cache write means acceptance into RAM. `E_OK` on flush means that all
pending sectors were accepted by their drivers. Neither proves physical-media
persistence through volatile device caches or atomic filesystem metadata updates.
The block-device interface has no hardware flush/barrier method.

High water and the yield-driven deadline trigger attempts; they are not hard
volume/time limits when the device fails. At most 64 dirty sectors can remain.
A cache miss attempts each dirty victim at most once, but the elapsed time still
depends on each driver's timeout. Repeated calls can retry; permanent device loss
can pin all 64 slots until recovery. Failing closed is intentional, and there is
no automatic discard command. Reads of accepted sectors remain available while
the descriptor's geometry and read capability are valid.

Block-device descriptors must outlive cached sectors and must not be reused for a
different physical medium before successful detach. SMP, concurrent kernel cache
access, hotplug identity generation and the VFS's broader error/transaction model
are outside this bounded package.

## Discovered During Implementation

| Priority | Finding / evidence | Impact and action |
| --- | --- | --- |
| P0 | Lifecycle syscalls ignored flush results; `init_fs()` calls `bcache_init()` | Accepted bytes lost at mount change or shutdown. Fixed error propagation and abort paths; fault-injection tests exercise all five syscalls. |
| P0 | Cache null-buffer hits/misses and non-512-byte sectors lacked checks | Kernel memory corruption possible through malformed internal requests or a future driver. Added admission guards and negative tests. |
| P0 | `blockdev_read()` trusted registration to imply a non-null handler, but direct callers can supply unregistered descriptors | Null indirect call. Shared direct/cache validation now returns `E_NODEV`; covered in both modules. |
| P1 | `test_bcache.c` wrote sectors 1024 and 4000+ on the root, assuming they were unused | Test safety depended on image layout. Replaced with two RAM devices, checking all 512 bytes and cleaning their cache entries. `test_mount.c` still uses scratch sectors on the isolated QEMU images; broader conversion remains. |
| P0 | `init_fs()` still autoformats based on a header-region blank check and boot tries alternate disks | A disk with data outside the checked region can be misclassified. Do not test on user disks; next package must separate mount from explicit initialization and remove ambiguous selection. |
| P0 | VFS metadata writes and some flushes discard status; temporary-file swap is not crash atomic | Cache correctness alone cannot guarantee successful filesystem operations or power-loss recovery. Next package must propagate metadata errors, add a read-only checker and run power-cut tests before claiming durability. |
| P0 | `make run` still truncates working-directory disk/USB images | Not executed. Only kernel-test targets under verified `build/testrun/` are used. Guard interactive image recreation separately. |
| P1 | Included `.d` files selected a single object as the default Make goal | VM `make -pn` reported `build/prod/kernel/core/kernel.o`; libc reported `../build/prod/lib/ft_atoi.o`. Added `.DEFAULT_GOAL := all` to both Makefiles, preserving existing work. Plain `make` passed. VM dry runs with `-W fs/bcache.c` and `-W ft_bzero.c` confirmed that default builds reach ISO creation and libc archiving respectively; matrix validation completed with the unrelated RTC failure listed below. |
| P1 | rsync preserved a Mac source mtime older than an object compiled during the baseline | First negative-control attempt failed at link time with undefined `blockdev_check_read/write`. Content hashes matched, but Make reused old `blockdev.o`. Forced its source newer with Make `-W drivers/blockdev.c` in the test-build submake; force the changed sources on final verification too. This is a build preparation failure, not a successful negative control. |
| P2 | README contains pre-existing stale counts/ABI prose despite newer source | Version badge/status and ISO names were reviewed; this package updates cache/test documentation and adds an Unreleased entry without inventing release totals. Reconcile all release inventories before publication. |

## Regression coverage

The `bcache` module uses two static RAM-backed devices and checks:

- Cache hits, overwrite accounting, deadline and high-water persistence.
- Boundary/max LBAs, null pointers on hits/misses, read-only devices, absent read
  handlers, changed capacity, unsupported sector sizes and no side effects.
- Failed reads are not cached. Each of five write errors preserves data through
  both global and device-specific flushes, and recovery persists exact bytes.
- Same-LBA isolation, independent device flush and recovery routing.
- Clean victims behind a failing oldest slot, all-64-slot exhaustion, bounded
  read/write refusal, a later recoverable dirty victim and complete recovery.
- Sync/reboot/halt/unmount/mount errors preserve dirty data and mount identity;
  successful sync after recovery persists the original data.

The blockdev module additionally checks null write buffers, missing read handlers
on direct requests, and side-effect-free request validation. Test devices are not
registered and never become the root. The synthetic task's UID and user scratch
bytes are restored. No new assertion depends on BIOS versus q35 versus UEFI.

## Transfer manifest and verification

Initial sync: 39 explicitly listed existing/planned source files. All Mac `md5 -q`
and Linux `md5sum` results were nonempty, successful and equal before and after
`rsync -avR`. The verification mechanism rejected both deliberately different
files and two empty hashes. Subprocess errors are fatal.

New files:

- `docs/engineering/RELIABILITY_AUDIT.md`

Modified by this package:

- `CHANGELOG.md`
- `README.md`
- `SECURITY.md`
- `Makefile`
- `lib/Makefile`
- `drivers/blockdev.c`
- `fs/bcache.c`
- `include/bcache.h`
- `include/blockdev.h`
- `kernel/syscall/sys_mount.c`
- `kernel/syscall/sys_sec.c`
- `tests/kernel/test_bcache.c`
- `tests/kernel/test_blockdev.c`

Proposed deletions: none. The other pre-existing modified/new source files remain
unchanged by this package. No instruction or HANDOFF content is transferred.

Transfer form, executed on the Mac using the explicit manifest above:

```text
rsync -avR -e 'ssh -p 2222 -o BatchMode=yes -o ConnectTimeout=10' -- <manifest paths> esduman@localhost:/home/esduman/kernel/
```

Final source hashes (Mac `md5 -q`, independently compared with VM `md5sum`):
| Source | MD5 |
| --- | --- |
| `CHANGELOG.md` | `01dc28ec3b1f98168fb0f0791f5cc073` |
| `Makefile` | `35db2ae830092838bda9de314607b59c` |
| `README.md` | `ffc4f4afe2097c1cbd7fc76b3db0b8e1` |
| `SECURITY.md` | `c012b00e751734c606c19b1261c07901` |
| `drivers/blockdev.c` | `b1368ef7d32379a12a9583927374ef13` |
| `fs/bcache.c` | `7cb07fef319959463b83ed213ef37cd7` |
| `include/bcache.h` | `11749859f2a3c394f715827c6edc16cb` |
| `include/blockdev.h` | `897b11e57aeca45705866d828eb37d24` |
| `kernel/syscall/sys_mount.c` | `c7401306b6b54b3a1598eeab0b2c1ed3` |
| `kernel/syscall/sys_sec.c` | `daf94a656145b5e48276d9d0101c7d90` |
| `lib/Makefile` | `a0c4a8b19f864cdb3c445a132b187d52` |
| `tests/kernel/test_bcache.c` | `b6bce37e2d5aeb2d6c0320fd88747cfa` |
| `tests/kernel/test_blockdev.c` | `5bf6f8ffaa581cd75633a2dbb0e07842` |

The audit file is also transferred and verified. Its own checksum is recorded
in the external delivery report and verification JSON to avoid a self-referential
checksum in this document. The source manifest contains 14 unique files.

A content match alone does not invalidate old build objects: after editing while
a baseline compiles, rsync can install an mtime older than the baseline object.
Force the affected sources with Make `-W` in the actual `BUILD=test` submake
before accepting a test result; do not rely on a top-level flag being inherited.

## VM validation log

Logs: `/tmp/esduman-reliability-20261024/` on the VM. Baseline command:

```text
make test_kernel MODULE=blockdev,mount,bcache
```

Baseline result: **62 passed, 0 failed**, Make exit status 0. This is the agent's
filtered baseline, not an authoritative release total. The source
comparison and `git diff --check` are Mac-side static verification, not builds.

Additional completed VM checks: `make test` exited 0 (host unit and static
checks); `make fuzz` exited 0, processing **138285 inputs in 11 seconds** from
58 corpus files without a sanitizer finding. The fixed targeted command
`make test_kernel MODULE=abi,blockdev,mount,vfs,bcache` then passed **366 / 0**
with exit 0, after explicitly rebuilding all six changed C sources via `-W`
in the `BUILD=test` submake. The production command `make -j2 all` (with `-W`
for changed production C sources) built `esdumanOS-v1.13.0-beta.1.iso` with exit 0.
After the Makefile correction, both default goals resolve to `all`; plain
`make` exits 0 and the libc submake checks the complete archive. VM dry runs
confirmed that a changed cache source reaches `grub-mkrescue` and a changed libc
source reaches `ar rcs`, so both dependency chains are exercised. The full
BIOS/q35/UEFI/SMAP matrix completed with the RTC failure listed below. These
remain agent runs.

Full matrix command: `make test_matrix` (Make exit **2**, not a green gate).

| Configuration | Passed | Failed | Result |
| --- | ---: | ---: | --- |
| BIOS/i440fx | 1629 | 0 | Pass |
| q35/AHCI | 1628 | 1 | `test_time.c:220`: date read back after setting RTC differs |
| UEFI/GRUB | 1629 | 0 | Pass |
| SMEP/SMAP (`-cpu max`) | 262 | 0 | Pass; kernel-mode modules intentionally skipped |

The three full configurations execute **1629 total assertions each**. All storage
assertions pass. The single failure is the existing RTC round-trip assertion
already documented in README; passing reruns do not erase it. The cache patch
and this audit do not claim that the RTC is deterministic.

RTC characterization: the first isolated `make test_kernel_q35 MODULE=time`
repeat passed **65 / 0**. Repeats 2 and 3 also each passed **65 / 0**,
with Make exit 0 in all three isolated runs. One full-matrix q35 failure and
three isolated passes are the complete observed sample, not a failure-rate
estimate or proof that the problem is repaired. The failing full run
records the assertion and expected date (2030-06-15), but not the observed date or
raw CMOS registers. No claim is made about that missing state. The next RTC work
should capture it and investigate the unprotected CMOS selector/data pairs shared
with `rtc_timer_callback()` before attributing the failure to QEMU timing.
That is a source-based investigation lead, not a demonstrated root cause.

Negative control: updated tests with the **old cache** produced **23 failing
assertions** followed by a page fault at address `0x0` on the null-buffer test.
QEMU was bounded by `QEMU_TEST_TIMEOUT=180`; the panic halted the guest and Make
exited 2 on timeout. The earlier link failure is separately documented above and
is not counted as a successful negative control.

Images were restricted to the checked `build/testrun/` directories; no user disk
or interactive `disk.img` was used. RDRAND-only, power-cut, real hardware and
release-gate runs were not performed. No test tolerance was widened and no
assertion was disabled to obtain a passing result.

Fuzz/host execution left generated VM-only artifacts: one new corpus entry
`tests/host/corpus/0b87ffb5eeef9a5bc598909021b896b6cad80c06`, plus
`tests/host/fuzz_parser`, `tests/host/test_crypto`, `tests/host/test_elf_validation`
and `tests/host/test_hash` (and ignored build outputs). They are not source
changes, were not copied to the Mac and were not deleted or included in the
proposed commit manifest.

## Commit/release boundary

The initial single-commit proposal for these 14 files is superseded by the
user-requested v1.13.0 commit series prepared on 2026-10-24. The complete working
tree is partitioned into 28 commits covering 40 unique source/documentation
paths, including the pre-existing boot and driver work. Each path belongs to
exactly one commit. The external `v1.13.0-msgs/0-manifest.txt` records the ordered
file ownership; individual English messages and a PR body accompany it.

This is a preparation artifact, not an executed Git operation or a release
certification. Validation above applies to the final combined source tree;
intermediate commits are not independently certified to build or pass tests.
The PR body records the unresolved q35 RTC failure. Commit, push and merge
remain user operations. No tag or release is prepared or published here.

## Next work in impact/dependency order

1. Separate mount from format, require explicit device selection/initialization,
   and guard `make run` against overwriting user-provided images.
2. Propagate VFS metadata I/O errors, define transaction guarantees, build a
   read-only image checker, then reproduce crash consistency on isolated disks.
3. Characterize RTC flakiness with repeated runs and hardware-state evidence.
4. Validate controller recovery and safe combined USB boot/data layouts.
5. Move VFS state into a mount context before adding simultaneous mounts.
