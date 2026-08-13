# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.3.1-alpha] - 2026-08-13

### Fixed

- **`/bin` tools mis-parsed their arguments.** The shell pasted the current directory
  onto the front of every argument string as an implicit first token, joined with a
  space rather than a slash. In `/home`, `touch notes.txt` produced the argument string
  `/home notes.txt`, and `touch` — which treats its whole argument string as one
  filename — created a file called exactly that. Every tool except `echo` was affected;
  `echo` alone skipped the leading token, so its skip is removed alongside.

  The mechanism dates from when the kernel had no idea where a process was and each
  tool had to be told. v0.3.0 moved the working directory into the PCB but left the
  shell still prepending, so a bare name now resolves correctly on its own.

  Nothing caught this before release: every assertion called the syscalls directly,
  and the defect was in how arguments were assembled before any syscall ran. There is
  now an end-to-end test that execs a real tool with a bare filename and checks the
  file lands in the working directory and not in root.

- **The block cache announced every automatic flush.** Once v0.2.0 gave write-back a
  five-second deadline, the existing INFO line meant a console message every five
  seconds for as long as anything was being written. Flushes performed by the policy —
  the deadline and the high-water mark — now report at DEBUG; an explicit `sync()`,
  reboot or halt still reports at INFO.

## [0.3.0-alpha] - 2026-08-13

Moves the working directory into the kernel. Until now the shell kept it in a userspace
global and passed a directory id into every syscall that touched a path, so a process
chose where its own relative lookups started — and every `/bin` tool passed a hardcoded
0, which meant they all operated on the root directory regardless of where the shell had
`cd`'d to. `cd /home && touch foo` created `/foo`.

Test coverage: 321 → 341 assertions.

### Added

- **`chdir` (46) and `getcwd` (47).** `chdir` validates that the target is a directory
  the caller may read and only then commits, so a failed call leaves the process where
  it was. `getcwd` renders the path by walking the parent chain, bounded so a corrupted
  `parent_id` reports `E_NAMETOOLONG` instead of spinning inside a syscall.
- **`cwd_id` in the PCB**, inherited from the creating process alongside `uid`. `fork()`
  will rely on the same inheritance when it lands.
- Failures are now collected and reprinted as a block just before the tally, with
  `file:line` for kernel-mode assertions and a `ring3` tag for results reported across
  the syscall boundary. A full run prints several hundred lines and hunting the `[FAIL]`
  markers out of that scrollback was its own chore.

### Changed

- **`open`, `create_file`, `rm`, `mv`, `mkdir`, `cat`, `cat_raw`, `get_dir_id` and
  `exec` resolve relative paths against the PCB's working directory.** They no longer
  read a base directory from a register the caller filled in. This is an ABI change: the
  argument that used to carry the directory id is ignored.
- The `/bin` tools are fixed as a side effect, with no changes of their own — they were
  already passing 0 in that slot.
- The shell drops its `current_dir_id` global and roughly 45 lines of hand-rolled path
  canonicalisation — splitting on `/`, pushing and popping tokens to fold `.` and `..` —
  all of which `vfs_resolve_path()` already did. The prompt is refreshed from `getcwd()`
  rather than predicted, so the shell's idea of where it is cannot drift from the
  kernel's.
- `mv` across directories is now refused explicitly. `fs_rename()` renames within one
  directory and has no notion of moving between parents; ignoring the destination
  directory and renaming in place would have been the silent alternative.
- `init` execs an absolute `/bin/sh`. A bare name would resolve from init's own working
  directory — root — and never find the shell.

### Fixed

- **A VFS test had been asserting nothing since the test-mode pointer relaxation was
  removed.** `test_vfs_boundary_and_depth` passed kernel addresses to syscalls: a stack
  array for the directory name, string literals for the out-of-bounds cases.
  `validate_string_pointer` rejects those, so every `sys_mkdir` returned `E_FAULT`, the
  nesting loop broke on its first iteration, and the two assertions that followed passed
  vacuously — a backtrack starting at root has nothing to walk.
- Fixing that exposed a second bug the empty loop had been hiding: the parent walk
  scanned only the first 32 `dir_table` entries, with a comment claiming
  `MAX_FILES_IN_DIR` was 32 when it is 256, so it missed every directory a boot places
  past that index.
- Removed a djb2 checksum in `fs_create_encrypted()` that was computed over the plaintext
  on every encrypted write and never read. The HMAC-SHA256 tag is what detects tampering.

### Security

- User space can no longer nominate the directory a relative lookup starts from. The
  K-10 hardening added validation of the caller-supplied `parent_id`; this removes the
  input instead. The two regression tests that asserted a bogus id was rejected now
  assert that it has no effect, since the rejection they checked for can no longer occur.

## [0.2.0-alpha] - 2026-08-12

A security and correctness release. It is the result of a full read-only audit of the
v0.1.0 tree followed by staged remediation, and it changes the project's honest
maturity claim: before this release **every automated test ran at CPL=0**, so process
isolation, the scheduler and the syscall boundary were written but never exercised.
The suite now crosses into Ring 3 and runs under hardware SMEP/SMAP.

Test coverage went from 268 to 321 assertions, all passing, in both the default-CPU
and RDRAND configurations.

### Added

**Verifiability**
- Ring 3 test payload (`tests/user/ktest_user.c`) loaded as a real encrypted ELF into
  its own address space. It reports results through `SYSCALL_KTEST_REPORT` (200), which
  is serviced only in test builds and answers `-ENOSYS` in production kernels. This is
  the only part of the suite that crosses the privilege boundary.
- Test modules: `test_entropy.c`, `test_lifecycle.c` (address space clone/teardown with
  frame-leak accounting), `test_fault.c` (double-fault infrastructure), `test_elf.c`
  (loader validation). 23 kernel-mode modules total, up from 19.
- `make test_kernel QEMU_TEST_CPU="-cpu qemu32,+rdrand"` — reaches the `ENTROPY_OK`
  branch, which `make test_smap` cannot, because `-cpu max` also enables SMAP and the
  kernel-mode modules are skipped under it.

**Entropy (`kernel/security/entropy.c`)**
- Entropy pool: RDRAND first, otherwise interrupt timing jitter from the PIT, keyboard
  and ATA completions. Samples land in a lock-free ring written by interrupt handlers
  and drained with interrupts masked; mixing and extraction are SHA-256 based, with the
  state hashed forward after every extraction for backtracking resistance.
- Per-source **lifetime entropy budgets**, not just per-event caps. The PIT is credited
  zero (it is periodic), ATA is capped at 64 bits for the whole boot, and only the
  keyboard scales. Consequence, and intended: without RDRAND and without someone
  typing, the pool stays at `ENTROPY_WEAK` and says so.
- `entropy_get_stats()` reports observed jitter as statistics only — no raw sample
  leaves the pool — so the suite can measure what the hardware really produced.

**Durability**
- `SYSCALL_SYNC` (45): writes every dirty block-cache sector to disk. Unprivileged, and
  not gated on the security level.
- Block cache write-back policy with two independent bounds: a volume high-water mark
  (32 of 64 slots) enforced inside `bcache_write_sector()`, and a 5-second deadline
  evaluated from `sys_yield()`. `bcache_flush_is_due()` is side-effect free so the
  policy can be asserted without performing I/O.

**Fault handling**
- Double-fault task gate with its own TSS and stack, so kernel stack overflow is caught
  instead of triple faulting silently. The GDT grew from 8 to 9 entries.

**Devices**
- `/dev/urandom`, alongside a rewritten `/dev/random`. Both are ChaCha20 keyed from the
  entropy pool, re-keyed on output volume, on a time interval, and immediately when the
  pool's verdict improves — so a machine that gains entropy after boot benefits without
  restarting. Each request ratchets the context forward.

**API**
- `TIMER_HZ` in `rtc.h`, replacing a literal and three prose restatements of the PIT rate.
- The ISO is now named `esdumanOS-v<version>.iso`, with the version derived by the
  Makefile from the `OS_VERSION_*` macros in `include/kernel.h` — so a version bump
  touches one file. The kernel binary stays `myos.bin`, because `grub/grub.cfg` names it
  and a mismatch there yields an ISO that fails to boot rather than a build error.
- Incremental SHA-256 (`sha256_init`/`update`/`final`), PBKDF2-HMAC-SHA256
  (`crypto/pbkdf2.c`), standalone ELF validation (`kernel/proc/elf_validate.c`, also
  fuzzed), `trap_frame_is_live()`, `syscall_block_and_restart()`, `fs_dir_exists()`,
  `dev_index_is_valid()`, `kernel_master_key_available()`, `crypto_fs_key_is_usable()`.

### Fixed

- **Arbitrary function pointer call from unprivileged code.** `get_device_idx()` reported
  failure as `E_NOENT` (-2) while `sys_read`/`sys_write` compared against -1. The
  descriptor was handed out carrying `ptr == (uint32_t)-2`, and the first read or write
  on it evaluated `dev_table[-2].read` and called through whatever lay in front of the
  table. Reachable by any process.
- **Every user frame leaked on process exit.** `cleanup_process_memory()` was guarded by
  a CR3 equality check that was always true at the call site, so it never ran. Exit now
  hands the task to a zombie list and a reaper in `schedule()` tears the address space
  down once another directory is live.
- **`clone_page_directory()` returned `E_NOMEM` as a physical address** on failure, so
  the caller loaded `0xFFFFFFF4` into CR3.
- **`clone_page_directory()` marked PD[0..3] present at physical frame 0.**
- **`sys_readdir()` wrote to user memory without `copy_to_user()`**, which panics the
  moment SMAP is enabled.
- **`sys_exec()` wrote `regs->eax` after `sleep_current_task()`**, corrupting the
  *next* task's return value.
- **`parent_id` was taken from user space unvalidated**, and `check_vfs_access()` could
  loop forever walking parent links.
- **`schedule()` could `iret` into a task it had just found unrunnable.** With nothing
  runnable it did `sti; hlt; return`, leaving `*regs` untouched, so a task that had
  blocked carried on as though it never had. There is now an always-runnable idle task.
- **Locks were handed `&current_task->regs`** — a saved copy, not the live interrupt
  frame. Writing through it corrupted the stored context while the real frame went
  untouched. `trap_frame_is_live()` now rejects it.
- **`regs->eip -= 2` for syscall restart** assumed every frame was an `int 0x80` entry.
  Replaced with an explicit restart flag driven by the entry EIP the dispatcher records.
- **Kernel stack raised from 4 KB to 8 KB.** The deepest measured path, `sys_auth()`,
  reaches roughly 2.5–3 KB on its own before interrupts nest on top.
- **`schedule_kernel_timer()` had two contradictory declarations.** `process.h` declared
  `int schedule_kernel_timer(int ticks, int pid)`; `signal.c` defines
  `void schedule_kernel_timer(int timer_id, uint32_t delay_ticks)`. The two disagreed on
  the meaning of *both* parameters and on the return type, and `sys_alarm()` — which
  includes `process.h` — was compiled against the one that matched nothing.
- **`signal.h` declared three functions that existed nowhere**, under their pre-rename
  names (`register_signal`, `schedule_signal`, `signal_tick_handler`). Three translation
  units included it.
- **`sys_alarm()` printed "3 seconds" and scheduled 0.55.**
- **`include/types.h` did not compile under C23.** `typedef _Bool bool` is a syntax error
  there, and GCC 15 defaults to `gnu23`; the build pins no `-std`, so this file decided
  whether the tree compiled at all.
- **`.gitignore`'s blanket `*.bin` swallowed the fuzzer corpus seeds**, dropping inputs
  that had found real bugs while the hash-named seeds beside them stayed tracked.

### Changed

- **CryptoFS IVs are derived, not drawn.**
  `HMAC-SHA256(file key, "esdumanOS-iv-v1" ‖ counter ‖ pool bytes)`. The monotonic
  counter makes the input distinct on every call, so two files cannot share an IV even
  with a dead entropy pool; keying with the file key keeps it unpredictable. **The
  on-disk format is unchanged** and files written by earlier builds still decrypt.
- **Shadow salts are derived** as `SHA-256(pool bytes ‖ username)`, so two accounts
  cannot collide even if the pool were producing a constant.
- Password verification uses PBKDF2-HMAC-SHA256; `hmac_sha256()` no longer allocates
  per iteration, and the iteration count read from `/etc/shadow` is clamped to a sane
  range so a tampered file can neither weaken verification nor stall the kernel.
- `CR0.WP` is enabled: the kernel honours read-only user pages. `map_page()` builds new
  page directory entries from the requested flags rather than always user-accessible.
- SMEP/SMAP are enabled when the CPU reports them, with the CPUID leaf checked properly.
- LOCKDOWN now refuses encrypted VFS access after destroying the master key, instead of
  silently encrypting and decrypting with an all-zero key — which had been quietly
  destroying the filesystem — and it blocks starting new programs.
- The kernel's non-preemptibility is now documented as a deliberate invariant with its
  reasoning recorded next to the guard in `schedule()`, rather than looking like an
  oversight. Making the kernel preemptible requires fixing the frame layout, the
  lock-free bcache/pipe/ATA paths and the global uaccess state first.
- `make test_smap` no longer ends in `|| true`, and CI no longer marks it
  `continue-on-error` — a real SMAP regression now fails the build.

### Removed

- `switch_to_user_mode()` and its two declarations. It built an `iret` frame by hand and
  was called from nowhere; `start_first_task()` is the path tasks actually take.
- `include/shell.h`, `include/ft_printf.h`, `include/string.h` (included by nothing),
  `tools/test_passwd.c`, `tools/inject.py`, and a committed compiler artifact
  (`lib/ft_strstr-c36b9fff.o.tmp`).
- `-I src/libc` from `CFLAGS`; the directory does not exist.
- The fictional `schedule_kernel_timer()` declaration in `process.h`.

### Security

- The disk encryption key is a build-time constant compiled into the kernel. That is
  now stated plainly in the README and `SECURITY.md`: it gives tamper resistance, **not**
  confidentiality at rest.
- Entropy quality is reported honestly rather than assumed. Measured under QEMU TCG, the
  TSC advances in multiples of 1000, so only 4 of the 32 possible values of
  `delta & 31` ever occur — the low bits of every timing delta carry no information on
  that platform. An earlier iteration of this release credited 31106 bits from 15629 ATA
  completions and would have claimed cryptographic quality on a machine with none; the
  per-source budgets exist because that measurement was taken.
- `SECURITY.md`'s known-limitations list was substantially wrong in the *pessimistic*
  direction — it claimed no per-user salt and a non-standard KDF, both of which had
  ceased to be true — and wrong about a `1234` default password that exists nowhere in
  the tree. Corrected.

### Documentation

- Reconciled `README.md` against the code: 45 discrepancies. The build instructions
  named the wrong environment variable (`ESDUMAN_KEY` instead of
  `ESDUMAN_ELF_KEY_HEX`, which must be exactly 64 hex characters), so nobody following
  them could build. The PIT rate was documented as 1000 Hz in three places and is 100.
  Four of nine rows in the resource-limits table were wrong. Eight completed items were
  still listed as open on the roadmap. `make run` was described as opening a QEMU window
  with serial on stdio; it uses `-display curses` and logs to `kernel_log.txt`.
- Same corrections applied to `CONTRIBUTING.md`, which carried the same unbuildable
  `ESDUMAN_KEY` instruction.

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
