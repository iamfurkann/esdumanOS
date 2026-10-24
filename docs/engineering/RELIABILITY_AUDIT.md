# Reliability audit

Audit date: 2026-10-24 (Europe/Istanbul).

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
