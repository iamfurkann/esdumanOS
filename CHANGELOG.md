# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.5.1-alpha] - 2026-08-15

No kernel code changes. 503 assertions, unaltered — the point of this release is that
getting to them stops costing two minutes.

### Fixed

- **Test and production objects no longer share a tree.** They are compiled from the same
  sources with different flags — test builds carry `-DPBKDF2_DEV_ITERATIONS` — and make
  cannot see a flag change: it compares timestamps, and a differently-compiled object of
  the same age looks current. The only defence was deleting every object before each test
  run, which is why `make clean` was mandatory and why a full rebuild was the price of
  running the suite at all.

  Worse than slow, it was a live hazard in the other direction: a release image built
  without `make clean` first linked whatever the last test run had left behind, and
  inherited its reduced iteration count. The Makefile carried a warning saying exactly
  that, and named this fix.

  Objects now go to `build/<flavour>/`, mirroring the source tree — `prod`, `test` and
  `dev`, the last for `make run-dev`, which had the same problem and was reducing PBKDF2
  cost directly into the production tree. `make` and `make test_kernel` can now be run in
  any order, and a one-file change recompiles one file.
- **`lib/libc.a` was shared across all three flavours too.** No flag that currently
  differs reaches libft, so nothing was wrong today — but one archive serving builds
  compiled differently is the hazard this release exists to remove, and it would have
  been found the hard way the first time that stopped being true. It builds into the
  per-flavour tree with everything else.
- **libft was never rebuilt when a header changed.** `lib/Makefile` compiled with the
  `-MMD -MP` the parent exports, generated a `.d` file for every object, and then never
  read them. A change to a header under `include/` rebuilt every kernel object that used
  it and left libft's alone, so the archive could carry objects compiled against a
  version of a header that no longer existed — the kind of mismatch that surfaces as a
  struct with the wrong layout, a long way from the change that caused it. The
  dependency files are included now.

### Added

- **`make test_kernel MODULE=<name>` runs one module instead of all of them.** Measured
  on the development machine, the build split took a no-change test run from 2m40s to
  1m44s — and the remainder is almost entirely QEMU, which no build change can touch. The
  host is an ARM laptop emulating x86, so the suite runs inside an emulator inside an
  emulator; the only way further down is to run less.

  The module list became a table so it can be searched as well as walked, and the order
  is still the order, so a full run is unaffected. `MODULE=ring3` runs the user-mode
  payload alone. An unknown name prints the available ones and fails, rather than
  executing nothing and reporting a pass — which is what a typo would otherwise look
  like. CI passes no `MODULE` and never will: a filtered run proves one module, not the
  tree.

### Changed

- `make clean` removes `build/` in one step rather than enumerating object paths, so a
  file added to the build no longer has to be remembered in two places. It also sweeps
  the objects left at the old in-source locations — a tree built before this release has
  around 180 of them and the new `rm -rf build` reaches none — plus `qemu.log`, the host
  SAST binary and the Python bytecode the mkfs test writes. The old paths are derived
  from the source lists rather than found with a wildcard, so `clean` names what it
  deletes instead of sweeping for anything that looks like an object.

  `.vscode/` and `compile_commands.json` are deliberately left alone. They are editor
  state, not build output, and deleting the compilation database would silently break
  code navigation until someone thought to run `bear -- make` again.
- The `lib` sub-make runs with `--no-print-directory`. It is still invoked on every
  build, deliberately: making that conditional on `lib/*.c` would skip it when only a
  header had changed, and the sub-make is the only thing that knows its own
  dependencies. With the dependency files now read it does nothing, and says nothing,
  unless there is something to do.
- `kernel_log.txt` is ignored. `make run` writes QEMU's serial output there and it has
  been showing up as untracked ever since.

## [0.5.0-alpha] - 2026-08-15

A process can be made from a process. 503 assertions, 0 failures, up from 490.

Every process in this system has so far come from a file: `exec` builds an address space,
fills it from an ELF image, and blocks the caller until the result exits. That is enough
to run a program and not enough to run two — which is why the shell executes `cmd1 | cmd2`
one stage at a time and deadlocks when the first stage outgrows the pipe buffer.

**Scope is the kernel and its tests.** The shell still runs everything through `exec`.
Moving it onto `fork` is the next release, deliberately separate: building the shell on
an unverified `fork` would mean testing two unknowns at once.

### Added

- **`fork()` (syscall 53).** The child gets a private copy of the parent's user pages,
  its open descriptors, its working directory, uid, signal handlers, priority and FPU
  state, and returns from the call as if it had made it itself — 0 in the child, the
  child's pid in the parent. Everything that can fail happens before the child becomes
  visible to the scheduler, because a task already on the run list cannot be un-created,
  only reaped.
- **`wait()` (syscall 54)**, with a fixed table of parked exit statuses. `exec` never
  needed one: it blocks the caller before the child can run, so a parent is always
  already waiting by the time `reap_task()` delivers. A forked child exits whenever it
  likes, and a status dropped at that point is one `wait()` could never return. Both
  orders now work — the parent arriving first, and the child finishing first. A parent
  with no children left gets `E_CHILD` rather than a block that nothing would end.
- **`copy_user_space()`**, the half of `fork` that copies memory. Two passes, because no
  single directory can see both sides: with the parent's directory live every source page
  is readable at its own address and is copied into a fresh frame through
  `TEMP_MAP_VADDR`, then the clone is loaded into CR3 and the recorded pages are installed
  with `map_page()` — which already builds intermediate tables, sets U/S bits and rejects
  conflicts. Hand-rolling that would have been a second implementation of it.
- `tests/kernel/test_fork.c` and a Ring 3 `fork`/`wait` section in the test payload, 24
  assertions between them. The ones that matter are negative: a `fork` that shared frames
  instead of copying them would pass every content check and fail later, as two processes
  overwriting each other or as a double free at teardown.

### Changed

- **`inherit_fd_table()` is shared between `exec` and `fork`.** It was inline in the ELF
  loader; both callers need the same reference-counted copy, and a descriptor whose
  refcount is not taken means the first of the two tasks to exit destroys the pipe or
  commits and frees the file out from under the other. Standard descriptors are still
  defaulted in the loader alone — a fresh image needs them opened, a fork inherits the
  parent's table verbatim, closed entries included.

### Security

- **`auth_fail_ticks` is inherited by a forked child.** It is the cooldown `sys_auth()`
  imposes after a failed password attempt. A child starting with it clear would let a
  caller fork its way out of the delay and keep guessing at full speed — the copy is a
  rate limit, not context, and it is the one PCB field here that is carried over for a
  reason that is not continuity.

### Known issues

Copies are eager rather than copy-on-write. A child duplicates every page its parent had
mapped at the moment of the call, which is correct and more expensive than it needs to be.
COW requires reference counting on physical frames, and `cleanup_process_memory()` frees
every user frame it finds unconditionally — a shared frame would be released twice. That
is a change to the teardown path and a piece of work in its own right.

The pipeline deadlock is unchanged, because the shell has not moved yet. So is everything
else on the shell side: `rm` on a directory orphans its contents, every `~` expands
through one shared buffer, `grep` reads only the first 511 bytes, and `/bin/rm`,
`/bin/mv` and `/bin/kill` stay shadowed by builtins.

## [0.4.6-alpha] - 2026-08-15

Housekeeping before `fork`. No kernel code changes and no assertion count change — this
is entirely about the build environment and what it tells you when it breaks.

### Fixed

- **`make fuzz` now says why it cannot run instead of dying unreadably.** libFuzzer's
  runtime computes the hamming distance between compared values with the `POPCNT`
  instruction. A CPU that does not implement it raises `SIGILL` inside
  `__sanitizer_cov_trace_const_cmp8` — before any code in this project runs — and
  libFuzzer's own handler reports that as `deadly signal` without ever naming the signal.
  The stack trace points at the fuzz harness, which is the one place the fault is not.

  This is reachable on an emulated x86 host: QEMU's default `qemu64` CPU model omits
  `POPCNT`, so developing on Apple Silicon hits it unless the VM is started with
  `-cpu max`. Real hardware and the CI runners are unaffected, which is why it took a
  host change to surface at all. The target now checks `/proc/cpuinfo` first and prints
  the cause and the fix.

### Changed

- **CI runs on `ubuntu-24.04` rather than `ubuntu-latest`.** The floating label moves onto
  the next LTS on GitHub's schedule, and that swaps the toolchain under a tree nobody
  touched. This project builds with `-Wall -Wextra`, where a major GCC or Clang bump
  reliably surfaces new diagnostics — worth running deliberately, not discovering from a
  red build on an unrelated pull request.

### Documentation

- The `POPCNT` requirement for emulated hosts, in README's Requirements section, with the
  one-line check that confirms it.
- A note that the build host may be 64-bit — `gcc-multilib` is what makes that work and is
  what CI has always used, but the README never said so outright.
- The packages a minimal Debian netinst leaves out that the build needs: `make`, `git`,
  and `xxd`, which the ELF embedding step calls.

## [0.4.5-alpha] - 2026-08-15

Process lifecycle groundwork for `fork`/`wait`, and the bug that groundwork turns out to
fix. Nothing here is new functionality — it is the ability to end a task without being
that task, which the kernel simply did not have.

`exit_current_process()` welded three jobs into one body: release the task's resources,
publish its status to a parent blocked in `wait()`, and switch away from it. `wait()`
needs the first two without the third, so the split was going to happen inside v0.5.0
regardless. Doing it here means the `fork` patch carries one unknown instead of two, and
it means the split arrives with its own tests.

### Fixed

- **`kill` did nothing to a process that had not registered a handler.** `send_user_signal()`
  set a pending bit, and `check_and_deliver_signals()` cleared that bit again with no
  handler to hand it to — so a signal to a process that had never called `signal()` was
  recorded and dropped. Every process is such a process by default, which made `kill(1)`
  a no-op against exactly the runaway task a user needs it for. `SIG_KILL` and `SIG_TERM`
  now terminate a target that has not handled them, with an exit status of `128 + signal`
  — the same encoding the page fault handler already used for `SIGSEGV`.

  A target that is not the running task is reaped where the signal is sent. That is sound
  because the kernel is not preemptible: any task other than the current one is parked at
  a syscall or interrupt boundary with its frame saved in its PCB and nothing live on its
  kernel stack, so there is no context to unwind.
- **`create_process()` left four PCB fields holding whatever the kernel heap last put
  there** — `cmd_args`, `fpu_state`, `signal_saved_regs` and the mailbox. Nothing read
  them before writing them, so it never showed. It would have showed in v0.5.0: `fork()`
  copies a PCB as a whole, and the child would have inherited heap garbage rather than
  its parent's state. The PCB is zeroed at allocation now, which also subsumes the two
  hand-written loops that used to clear the kernel stack and the register frame.
- **A pid in use could be handed out a second time.** `next_pid` only moved forward and
  reset to 2 on overflow, with nothing checking what it landed on. `kill()`, the
  foreground bookkeeping and the parent search all identify a task by pid and stop at the
  first match, so two tasks sharing a number would send signals and exit statuses to
  whichever sat earlier in the list. Two billion `exec()` calls away in practice — but
  `fork()` is what makes pids cheap enough to spend, and the check belongs in the function
  `fork()` mirrors. Zombies are scanned alongside live tasks: a zombie still carries the
  pid its parent has yet to be told about.
- **A mutex held by a killed task is released.** `mutex_unlock()` identifies the owner as
  `current_task`, which was the same thing while a task's locks could only be dropped by
  its own exit. Reached from `kill()` it is not: the ownership test would be made against
  the killer, fail quietly, and strand the lock on a task that no longer exists — and
  nothing revisits a mutex afterwards, so every later waiter would block for the rest of
  the boot. `mutex_release_owned_by()` takes the owner as an argument; `mutex_unlock()` is
  now the guarded entry point that passes `current_task` to it.
- **Killing a background task no longer takes the terminal from the shell.**
  `foreground_task` was reassigned on every exit, which was indistinguishable from the
  correct rule while the only way to die was to be the running — and therefore foreground
  — task. Now the terminal moves when its holder dies, or when a shell blocked on the
  dying task wakes to take it back.

### Added

- `reap_task()` — everything a task's death entails except leaving it. The address space
  is still not freed there; the zombie reaper in `schedule()` does that once another
  task's directory is live.
- `apply_default_signal_action()`, called from exactly one place: the end of
  `syscall_handler()`, after the syscall bookkeeping is closed out. A task that signalled
  itself fatally cannot be reaped at the point the signal is sent — that code is running
  on its own kernel stack — so it is terminated on the way back out to user mode.

  Deliberately *not* folded into `check_and_deliver_signals()`, which is also called from
  the tail of `schedule()`: terminating a task from there would re-enter `schedule()`
  through `exit_current_process()`. For the same reason `check_and_deliver_signals()` now
  leaves an unhandled fatal signal pending instead of clearing it.
- `SIG_KILL` and `SIG_TERM` in `signal.h`. Both numbers were already in use — `kill(1)`
  sent a bare `9` with a comment explaining what it meant — but nothing named them.
- `tests/kernel/test_reap.c`, 31 assertions. Victims are given a real cloned address
  space rather than the fabricated `cr3` the other scheduler tests use: the zombie reaper
  loads that directory into CR3, and a made-up value there is a triple fault rather than
  a failed assertion.
- `tests/user/ktest_signal.c`, a Ring 3 payload that sends itself `SIG_KILL`, with the
  parent asserting the status comes back as 137. The self-signalled path runs only in
  `apply_default_signal_action()`, which is reached only on the way out of
  `syscall_handler()` — and the kernel-mode modules run against a synthetic task that
  never returns through it, so nothing there could cover the half of the default action
  that made restructuring `check_and_deliver_signals()` necessary in the first place.
  Same arrangement `/bin/ktest_crash` already has for the page-fault path, and embedded
  in the test image only.

### Security

- **The idle task is not reapable.** A working `kill()` made it reachable for the first
  time, and `schedule()` reaches the idle task through a named pointer rather than through
  the run list — so reaping it would have left that pointer aimed at memory the zombie
  reaper had already freed, giving an unprivileged `kill 0` a use-after-free on the way
  into a panic. Refused in `reap_task()`, so every caller is covered.

### Known issues

The pipeline deadlock is unchanged and still reachable: the shell runs `cmd1 | cmd2`
sequentially, so a first stage producing more than 4 KB blocks with no reader. `fork` is
the fix, in v0.5.0. `rm <dir>` still orphans its contents, `~` still expands through one
shared buffer, `grep` still reads only the first 511 bytes of a file, and `/bin/rm`,
`/bin/mv` and `/bin/kill` are still shadowed by builtins — all shell-side, and all
deliberately left for after `fork` lands, since the shell is rewritten for concurrent
pipelines then anyway.

## [0.4.4-alpha] - 2026-08-14

Header tidy-up before `fork`/`wait`. No behaviour change: the 445 assertions pass
unaltered, which is the point — the compiler is this release's test.

All 43 headers were audited. The good news first: **no circular includes and no guard
macro collisions.** The header graph is a clean DAG. The problems were in declarations.

### Fixed

- **`timer_ticks` was declared without `volatile`.** `arch/x86/cpu/timer.c` defines it
  `volatile` and increments it from IRQ0; `rtc.h` declared it plain. The tree builds at
  `-O2`, so a loop waiting on the counter through that declaration could have had the
  load hoisted out of it and spun forever. Nothing reads it that way today — every
  caller goes through `timer_get_ticks()` — so this was a loaded gun rather than a live
  miscompile, and it is the same defect class as the historical
  `schedule_kernel_timer()` declared twice with disagreeing signatures.

  It survived because **`timer.c` included none of the headers that declare what it
  defines**, so no definition was ever compared against its declaration. That is fixed
  alongside it: the file now includes `rtc.h`, `isr.h` and `signal.h`.
- **The last three lines of `rtc.h` sat outside the include guard**, after the `#endif`.
  Repeated `extern` declarations are legal so nothing broke, but the first typedef or
  inline added there would have broken every translation unit that includes it, at once.
- **Four ELF blob lengths were declared `const uint32_t` where the other eleven are
  `unsigned int`** — and `xxd -i`, which generates all fifteen, emits `unsigned int`. So
  four disagreed with their own definitions. Incompatible types across translation units
  is undefined behaviour the linker cannot catch.
- **`init_elf.h` used `uint8_t` with no `#include` at all.** It compiled only because its
  one consumer includes `kernel.h` on the line above; swapping those two lines broke the
  build.
- **Host tests were compiled with `-I./include`, which made `<stdio.h>` resolve to the
  kernel's own header** rather than the host's. Measured both ways: with `-I` a bare
  `printf` call fails to compile; with `-iquote` it reaches the host libc. The rules use
  `-iquote` now, so quoted includes still find the kernel headers and angled ones do not.
  A dead `-I./crypto` went with it — there are no headers in that directory.

  The three host tests already declared `printf` by hand and `test_crypto.c` carried a
  comment saying system headers had been removed, while still including `<stdio.h>`. The
  include is gone and the hand-written declaration is what remains, which is what the
  comment always claimed: this project ships no third-party library, and a host test
  borrows exactly one symbol to print a result.

### Removed

- Six declarations of things that do not exist: `init_signals()` (renamed to
  `init_kernel_timers()` and the declaration left behind), `auth_fail_ticks[16]` (a
  global-era leftover — it is a per-process PCB field now), `errno` (the kernel returns
  negative `E_*` codes and never had one), `__bss_end` (the linker script provides
  `_bss_end`, with one underscore), and the unused `signal_t` type.
- Three duplicate declarations: `register_kernel_timer` and `process_pending_kernel_timers`
  (both owned by `signal.h`), and `init_elf`/`init_elf_len` (owned by `init_elf.h`).
- `src/init_elf.h` — empty, unguarded, and not even on the include path.
- Two unused header includes (`pipe.h` → `registers.h`, `crypto.h` → `arch.h`) and 20
  unused includes across the kernel sources, each verified symbol by symbol.

### Changed

- The 49 assembly interrupt stubs are declared together in `isr.h`. Fourteen were there
  and thirty-five in `idt.c`, in two blocks a refactor script had appended at different
  times — exactly complementary, which is the giveaway that nobody chose the split.

### Deliberately not done

`kernel.h` is still a god header: 23 includes for symbols it does not use itself, pulled
in by 11 files, several of them only as a route to `stdio.h`. Splitting it ripples
through every consumer and does not belong in a patch. The same goes for moving
`fs_max_sectors` out of `bcache.h`, moving `klog_write_char`/`dump_klog` into `klog.h`,
and adding the ~20 direct includes that would let the load-bearing chains
(`libft.h` → `kheap.h`, `stdio.h` → `tty.h`, `serial.h` → `io.h`) be trimmed. Those have
an ordering dependency: the direct includes must land before the chains are cut, or the
build breaks.

## [0.4.3-alpha] - 2026-08-14

The kernel can write to a file through a descriptor. Two defects recorded in v0.4.2
came from its absence, and both close here.

### Added

- **`write()` on a regular file descriptor.** `sys_write` handled the console, pipes
  and `/dev` nodes; a regular file fell through to `E_BADF`. That is why `/bin/cp`
  produced an empty destination — it opened, read, wrote and closed correctly, and the
  kernel discarded every write — and why the shell's `>` could never be connected to
  anything.
- **`open()` honours its mode argument.** It was read from nowhere and every descriptor
  was marked read-only, so even `/bin/cp` passing `O_WRONLY` had no effect. Opening for
  writing truncates.
- **Output redirection in the shell.** `cmd > file` creates the target if it does not
  exist, empties it if it does, and sends the command's standard output there.

### How writing works, and why it is narrow

Writes are buffered in the kernel and committed as the file's entire new contents when
the **last** descriptor referring to it closes. That is not a shortcut taken for
convenience: under `SEC_LEVEL_CRYPTO_ENFORCED`, which is the default, a stored file is a
single AES-CBC blob with an HMAC over its whole plaintext, so adding one byte at the end
means re-encrypting and re-authenticating all of it. The VFS has no streaming write
primitive either — `fs_atomic_update()` replaces a whole file and is the only way in.

What follows from that, and is documented rather than half-emulated:

- No appending (`>>`), no writing into the middle of a file, no seeking during a write.
- A file written this way is capped at 64 KB, because the buffer is held in the kernel
  heap until the commit.
- `close()` returns the commit's result. A full disk or a destroyed master key surfaces
  there and nowhere else, so a caller that discards it turns a failure into silent data
  loss — the same shape as the `cp` defect this release fixes.

The commit is tied to the last reference rather than to every `close()`, because `dup2()`
can point several descriptors at one open file; committing on each would publish a
partial buffer. A process that exits still holding a written file commits it too.

### Tests

Test coverage: 411 → 445 assertions.

The Ring 3 half asserts what the semantics actually are: the file on disk is unchanged
while a descriptor is open, `close()` commits it, the bytes survive the encrypt/decrypt
round trip, opening for writing truncates, a read-only descriptor refuses writes, and —
the assertion the design turns on — closing one of two duplicated descriptors does not
commit while closing the second does. It finishes by running `/bin/cp` through `exec`
and checking the copy is the same size as the original, because the defect that made
v0.3.1 necessary lived in argument handling rather than in any syscall.

The size cap is checked from the kernel side instead: reaching it from user space takes
256 syscalls and then commits a 64 KB file, while calling `fs_write_buffered()` directly
costs nothing because the bound is checked before anything is allocated.

### Known and still not fixed

- **The pipeline deadlock is now reachable.** v0.4.2 recorded it as latent because
  nothing could produce more than 4 KB into a pipe. A program can now write that much,
  and the shell runs the two stages of `cmd1 | cmd2` sequentially — so a first stage
  that emits more than the 4 KB pipe buffer will block with no reader running. The real
  fix is `fork` (v0.5.0).
- `>` and `|` cannot be combined; the parser takes whichever it meets first.
- `kill` still has no default action for a process with no handler registered.
- `rm <directory>` orphans the directory's contents.
- Every `~` in one command expands into the same shared buffer.

## [0.4.2-alpha] - 2026-08-14

A stability patch. No new features: the whole v0.4.x tree was audited before starting
work on `fork`/`wait`, and this ships what that audit found. Eight of the defects
below are reachable from an ordinary unprivileged prompt, and three of them hang or
halt the machine.

### Fixed — kernel

- **A segfaulting program left its parent blocked forever and leaked its address
  space.** The page-fault handler's entire teardown was `state = TASK_DEAD` followed
  by a reschedule, so `exit_current_process()` never ran: descriptors were not
  released, a parent waiting on `WAIT_CHILD` was never woken, and the task was neither
  unlinked from the run list nor placed on the zombie list — so the reaper never freed
  its `process_t`, page directory, page tables, user stacks or descriptor table. In
  practice a user program with a null-pointer bug left the shell that started it parked
  on `WAIT_CHILD` with no console. It now exits through the same path `exit()` uses,
  with status 139 (128 + SIGSEGV).
- **Closing a pipe never woke the process blocked on it.** `wakeup_tasks(WAIT_IPC)`
  was called only when data moved, so a reader waiting on an empty pipe whose last
  writer went away never received the EOF the code was ready to give it, and a writer
  parked on a full pipe whose reader disappeared waited for the rest of the boot. All
  three release sites — `close`, `dup2` and process exit — now wake waiters.
- **`open()` dereferenced an unchecked `kmalloc()`.** Exhausting the kernel heap and
  then opening any existing file turned a NULL return into a supervisor write to
  address 0 — a kernel page fault with no fixup, which parks the CPU. Any unprivileged
  process could halt the machine.
- **`create_process()` published a runnable task with a NULL descriptor table.** The
  allocation failure skipped only the initialisation loop; the task was linked in and
  scheduled anyway, and its first `read`/`write`/`open` indexed a NULL table from
  Ring 0. It now frees the task and reports `E_NOMEM`, matching how the `process_t`
  allocation directly above it was already handled.
- **`fs_delete()` walked the FAT chain with no bound.** `file_allocation_table` holds
  4096 entries and the directory table is loaded from disk unvalidated, so a crafted or
  corrupt image gave `rm` a four-byte kernel write at an offset the image chose. Every
  other FAT walk in the file already had this check.
- **`dup2()` did not reference-count files.** Only pipes were counted, so
  `fd = open(f); dup2(fd, 5); close(fd)` freed the `vfs_file_t` while fd 5 still
  pointed at it: a use-after-free on the next read and a double free on the next close.
  Overwriting a file descriptor also leaked its old target. Both directions now behave
  the way the ELF loader's inheritance path already did.
- **The PIC interrupt masks were never programmed.** `pic_remap()` read the firmware's
  masks and wrote them back verbatim, and these were the only writes to the PIC data
  ports in the tree. It works under GRUB and QEMU only because that firmware leaves the
  lines open; a loader that masked them would leave IRQ0 and IRQ1 dead with handlers
  installed for both, hanging the first boot in the keyboard wait. The timer, keyboard,
  cascade and ATA lines are now unmasked explicitly.

### Fixed — shell and tools

- **The shell overran its own stack on ordinary input.** The tokenizer filled a
  32-entry argument array with no bound while the input line allowed about 127 tokens;
  33 short words were enough to corrupt `main`'s frame, and the pass that follows then
  read those slots back as pointers. Tab completion had the same shape twice more: the
  copy loops were clamped but the NUL terminators were not, so a long word or a long
  filename wrote past a 128- and a 64-byte buffer. The argument join and `~` expansion
  were likewise unbounded — and `~` expands to `$HOME`, so a short line could produce
  kilobytes.
- **Thirteen `/bin` tools exited 0 on every error path**, and eighteen shell builtins
  never set the exit status at all — so `rm /nope && echo GONE` printed `GONE`. v0.4.1
  connected the status chain but only `/bin/stat` was ever taught to use it. `cp`,
  `grep`, `head`, `kill`, `mv`, `rm` and `touch` now report failure, and every builtin
  sets a status.
- **`kill` parsed its arguments as hexadecimal**, so `kill 10 9` signalled PID 16, and
  a non-numeric argument silently became PID 0. It parses decimal and rejects junk.
  `/bin/kill` accepted zero digits and sent SIGKILL to PID 0 regardless.
- **`cp` reported success while producing an empty file.** The kernel has no write path
  for a regular file descriptor — `sys_write` handles the console, pipes and devices
  only — so every write returned `E_BADF` and the result was discarded. This does not
  make `cp` work; it turns silent data loss into a visible error. The missing capability
  is v0.4.3.
- **`su` failed silently** on a wrong password, leaving a fresh prompt and no clue.
- **`init` never detected a failed `exec`.** It tested for `-1`, which `exec` never
  returns — a missing `/bin/sh` is `E_NOENT`. PID 1 exited without a diagnostic on a
  broken disk. It now reports and parks rather than printing "System halted." and then
  falling through to exit.

### Documentation

Corrected against the code: the syscall count (the README said both 45 and 50), the
QEMU command's ISO name, the FPU description (eager, not lazy), the kernel log's disk
persistence (`/var/log` is created but nothing is written to it), the shell command
list (`export` takes two words, `su` takes none, `ls` takes none, `echo` and `clear`
are not builtins, twelve builtins were missing) and the supported-version table.

Output redirection is now documented as **not implemented** rather than as a working
feature. It is parsed and discarded, and it cannot work until the kernel can write to
a regular file through a descriptor.

### Tests

The crash-teardown path had no coverage because nothing in the image could produce a
user-mode page fault — every tool exits cleanly and the syscalls validate their
arguments rather than faulting. `tests/user/ktest_crash.c` is a Ring 3 program whose
only job is to write to address 0; it is embedded in the test kernel only and never
reaches the production image.

The Ring 3 payload now execs it and asserts the parent is woken with status 139, then
runs ten more crashes and compares the physical allocator's free-memory figure across
them. The second half matters as much as the first: the parent surviving does not prove
the dead task was reclaimed, and the leak was the larger half of the defect. If either
regresses the run hangs and hits the QEMU timeout rather than passing quietly, which is
the honest failure mode — a hang is the actual symptom.

### Known and deliberately not fixed here

- `kill` has no default action: a signal sent to a process with no handler registered
  for it does nothing, so a runaway process cannot be terminated. Terminating a task
  that is not the current one needs a safe path that does not exist yet.
- Writing to a file through a descriptor, and therefore `cp` and `>`.
- `rm <directory>` orphans the directory's contents.
- Every `~` in one command expands into the same shared buffer.
- Pipelines of more than two stages, and `|` combined with `>`, are mis-parsed.

## [0.4.1-alpha] - 2026-08-14

### Fixed

- **Exit statuses were discarded, so `&&` and `||` decided nothing.** `exit()` never
  read its argument, no field anywhere held a status, and `exec()` reported `E_OK` as
  soon as the child had *started*. The shell then recorded success unconditionally.
  The result was a shell whose conditional operators were decorative:

  ```
  # stat /no_such_file || echo FAILED
  stat: cannot stat '/no_such_file'
  # stat /no_such_file && echo CHAINED
  stat: cannot stat '/no_such_file'
  CHAINED
  ```

  `||` never fired and `&&` always did, whatever the command had done.

  All four links are now connected: `exit()` records its argument in the PCB, masked
  to the low 8 bits; `exit_current_process()` writes it into the waiting parent's
  saved frame; `exec()` returns it instead of `E_OK`; and the shell uses it. A failure
  to start is still a negative errno, and an exit status is 0-255, so the two cannot
  be confused.

  This needed no `fork()`. `exec()` already blocks the caller on `WAIT_CHILD` until
  the child finishes, which is functionally a `wait()` — the only thing missing was
  carrying the number back. The same plumbing is what a real `wait()` will use.

  Nothing caught it because every assertion checked that `exec()` succeeded, and by
  the old contract it always did.

- **`/bin/stat` reported a usage error as success.** Invoked with no argument it
  printed `stat: no file given` and exited 0. Harmless while statuses went nowhere,
  and wrong the moment they started arriving — `stat && echo CHAINED` printed
  `CHAINED` without a file having been named. Found by the first run of the fix
  above, which is the point of connecting the chain at all.

## [0.4.0-alpha] - 2026-08-14

Adds the syscalls a program needs to ask about things rather than only do them:
`stat`, `fstat`, `lseek`, `getpid` and `sleep`. Until now a program could open a file
but not learn its size or whether it was a directory, could read forwards but never
reposition, could not learn its own pid, and had no way to wait for a duration at all.

Two of the five needed groundwork. `stat` has to report the size a `read()` will
return, and that is not the size the directory table records — with encryption on by
default the stored form carries an IV, a header and padding — so sizes come from a new
VFS helper that reads the real length out of the file's header. And `sleep` is the
first production use of `WAIT_TIMER`, which had been defined since the beginning and
appeared in exactly one test.

Test coverage: 345 → 403 assertions.

### Added

- **`stat` (48) and `fstat` (49).** Both fill an `esd_stat_t` (`include/stat.h`),
  shared verbatim between the kernel and user space. `fstat` refuses pipes, the
  console and `/dev` nodes rather than answering for them: a device descriptor stores
  a device table *index* in the field a file descriptor stores a pointer in, and that
  overload is what once made a stale comparison in `open()` an indirect call through
  `dev_table[-2]`. There is deliberately no `st_mtime` — the on-disk entry has no
  timestamps and the RTC is not wired to the VFS, so one would have to be invented.
- **`lseek` (50)**, operating on `vfs_file_t.current_offset`, which was already the
  cursor both read paths use. `SEEK_END` asks the size helper, so seeking to the end
  of an encrypted file lands on the end of the data rather than inside the padding.
  Pipes and devices report `E_SPIPE`.
- **`getpid` (51)** and **`sleep` (52)**. `sleep` takes milliseconds: `TIMER_HZ` is
  100, so the resolution is 10 ms and a seconds-only call could not reach it. The
  shell's new `sleep` builtin still takes seconds.
- **`fs_size()` in the VFS**, branching on security level the way `fs_read()` does,
  refusal after LOCKDOWN included. For an encrypted file it decrypts one AES block
  rather than the whole file: `fs_create_encrypted()` writes the magic and the
  original length as the first eight bytes of the payload, so both sit in ciphertext
  block 0 and the IV in front of it is all CBC needs to get at them.
- **`/bin/stat`**, which prints the readable size and the on-disk size side by side
  so neither looks wrong on its own.
- **A test-build-only `KT_REPORT_TICKS`** on the existing report protocol, because
  Ring 3 has no clock and a `sleep()` that never blocked would otherwise satisfy every
  assertion a payload could make about it. The timing assertions measure.

### Fixed

- **Kernel timers could be counted down and then never run.**
  `process_pending_kernel_timers()` was called at the very end of `schedule()`, which
  put it behind the `current_task == next_task` early return. Whenever the same task
  was reselected — the ordinary case while only the idle task is runnable, with the
  shell blocked on `WAIT_KBD` — an expired timer waited for some unrelated task to
  become runnable before its callback fired. It now runs before the selection passes,
  on the caller's own stack and page directory instead of after CR3 has already been
  switched. The countdown and the drain had no test at all; they have one now.

### Notes

- `st_size` for an encrypted file is **not authenticated**. It is read from the file's
  own header, and the HMAC that would vouch for it covers the plaintext and is checked
  only by `read()` over the whole file. A tampered header can make `stat` report a
  wrong size — the read that follows fails, but the size alone proves nothing. Stated
  in the README and on the function itself rather than left to be discovered.
- `file_descriptor_t.offset` is dead for files; nothing on the read or write path
  consults it. Left in place, since the pipe and device paths do use it.

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
