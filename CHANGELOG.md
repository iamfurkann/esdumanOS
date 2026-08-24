# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.8.2-alpha] - 2026-08-23

v0.8.0 taught the terminal to take orders and said plainly that nothing in user space
sent it any. The consumer is a text editor, and an editor could not be written: the
arrow keys reached no program at all, a program could not tell the Escape key from the
start of an escape sequence, and one that had been stopped and resumed had no way to
learn it must redraw. This release is the input half, and the shell is its first user.

### Added

- **The navigation keys reach programs.** The keyboard sends what a terminal sends —
  `ESC [ A` through `ESC [ D` for the arrows, `ESC [ H` and `ESC [ F` for Home and End,
  and the numbered forms for Insert, Delete and the Page keys. Until now the arrows were
  bound to the scrollback and the rest landed on zero entries in the layout tables and
  were dropped without trace.

  A sequence is placed in the input ring whole or not at all. A reader handed the first
  two bytes of an arrow key has no way to know the rest was dropped: it waits for a byte
  that is not coming, or takes the next unrelated key as the tail of the sequence.

- **The shell edits the line.** Left and right move within it and typing inserts where
  the cursor is, Home and End jump to the ends, Delete removes forward. Up and down walk
  the last eight commands, and the line being typed is saved when you walk away from it.
  This is the first thing in the system to use the escape sequences v0.8.0 taught the
  terminal to draw.

- **`POLL` (63) asks whether a read would block.** One byte both ends the Escape key and
  begins a sequence, and there is no timer fine enough to tell them apart by how long the
  next byte takes. Asking whether a byte is already waiting does tell them apart, and it
  is the only question a program can ask that does not consume a byte it may have to give
  back. End of file counts as "would not block": a read that returns zero has returned.

- **`SIGTTIN` (21) stops a background job that reads the terminal.** There is one input
  ring and every blocked reader is woken when a key arrives, so such a job did not share
  the keyboard with the shell — it raced it for each keystroke, and half of what the user
  typed vanished into a job they had deliberately put out of the way. Stopping loses
  nothing: `jobs` shows it and `fg` gives it the terminal it was asking for.

- **`SIGCONT` is delivered to a process that registered a handler.** Being runnable again
  is the whole of the default action, but a program that draws has to repair what the
  shell wrote over its screen while it was stopped, and there was no moment at which it
  could find out that it had been.

### Changed

- **Scrollback moved from the arrow keys to Shift with the Page keys**, which is where
  every terminal emulator puts it. It had the arrows because nothing else wanted them;
  something does now, and a key the driver consumes is a key no program will ever see.

- **A process ending by signal no longer prints a line of kernel log.** Those records
  moved from `INFO`, which the console shows, to `DEBUG`. A process ending because the
  user pressed Ctrl-C is the user's own doing, and over a full-screen program the line
  lands in the middle of the display. The cost is worth stating: a `DEBUG` record at the
  default threshold is discarded rather than hidden, so it is not in the ring for `dmesg`
  either. Separating the console level from the ring level is the honest fix and belongs
  to a release about the log.

### Known issues

- **The line editor works within one screen row.** The cursor is moved with `CUB`, which
  cannot cross a row boundary, so a command long enough to wrap past column 80 can still
  be typed and run but is redrawn only on its last row.
- **`SIGTTOU` does not exist.** A background job that *writes* to the terminal still
  does, interleaving its output with whatever the shell is drawing.
- **A task that catches or ignores `SIGTTIN` reads from the background as before.** POSIX
  returns `EIO` there; a program that went to the trouble of catching the signal has said
  it knows what it is doing.
- **The Escape key does nothing at the prompt**, and the key pressed after it is held for
  one round while the shell decides whether a sequence is starting. Nothing is lost — the
  byte is put back — but a lone Escape has no effect of its own.

## [0.8.1-alpha] - 2026-08-22

v0.8.0 gave the user a way to stop what they had just started. This one gives them a
way to set it aside instead - which needs something the scheduler never had: a task
that is neither running nor waiting for anything, holding its memory and its place in
the program, until somebody asks for it back.

### Added

- **Ctrl-Z, `fg` and `bg`.** `SIG_TSTP` (20) goes to every process in the foreground
  group and parks it in a new task state, `TASK_STOPPED`. `SIG_CONT` (18) takes it out
  again. `fg` gives a job the terminal and waits for it, `bg` lets it run on without
  one, and both name a job as `%1` or by number - or nothing at all, which means the
  most recent.

  A stopped task remembers what it was doing. Almost every blocking syscall in this
  kernel resumes on the trap instruction and re-evaluates what it was waiting for, so
  such a task is released as runnable and repairs itself; `exec()` is the one that
  does not, because it returns through a register rather than re-running, and a task
  stopped inside one goes back into exactly that wait.

  Both signals are catchable, and that is load-bearing rather than incidental. The
  shell and init are both in the foreground group when no job is running, so a stop
  neither of them could decline would park the session with nothing left able to
  continue it. There is deliberately no `SIGSTOP`: an uncatchable stop would reach
  init the same way and nothing could be done about it.

- **`wait()` can report a child that stopped.** `WUNTRACED` (2) asks for it and
  `WNOHANG` (1) keeps its old meaning; the status comes back as `WSTATUS_STOPPED`
  (0x100) OR'd with the signal. Without the flag a stopped child stays invisible,
  which is the right default - a caller that knows nothing about job control would
  otherwise be handed a pid it would treat as finished for a process still very much
  alive.

- **`exec()` that hands back a pid.** A non-zero mode argument (`EXEC_NOWAIT`) starts
  the program and returns its pid instead of blocking for its exit status. The shell
  could not own a foreground command without it: never learning the pid, it could not
  put the program in a group of its own, could not hand it the terminal, and had no
  way to name a job the user had stopped. A foreground command is now exactly what a
  pipeline already was.

- **`kill()` with a negative pid signals a process group.** Needed to continue a job:
  the process the shell forked is often not the only member, because that process
  started the program the user is looking at and one Ctrl-Z stopped both. Continuing
  only the pid the shell knows about would leave it blocked on a task still stopped.

### Fixed

- **A parent inside `exec()` was woken by whichever child died first.** It used to be
  any child at all, which was the same thing only while `exec()` was the only way to
  have one. With background jobs it is not: `sleep 30 &` finishing while the shell sat
  in `exec()` returned the job's status as the foreground command's, so the shell
  printed a prompt with that command still running and both then read the keyboard.
  The pid being waited for is recorded, and the reaper delivers only for that one.

- **A task that never held the terminal could give it away.** Waking a parent moved the
  terminal to it whatever the dying task was — and a shell's `wait()` is woken by the
  job it is waiting for and by every other child it has. So a background job finishing
  while a foreground job ran took the terminal from the job on the screen, and the next
  Ctrl-C reached a shell that ignores it. The same rule that already kept a pipeline's
  terminal until its last stage exited now covers this: a group that still has a member
  keeps it, and a group that never had it cannot pass it on.

- **The `exec` builtin reported success for a program that failed.** It tested only
  for a negative return, so any exit status counted as 0. It now takes the same path
  as an ordinary command and reports what the program reported.

### Known issues

- **Ctrl-Z does not reach the guest under `-display curses`.** The key gets as far as
  the terminal QEMU runs in — `stty susp undef; cat -v` echoes `^Z` there — but the
  curses front end does not turn it into a guest keypress; Ctrl-C and Ctrl-D are
  delivered normally. The `^Z` echo is the first thing the driver does, so no `^Z` on
  screen means nothing arrived. The QEMU monitor's `sendkey ctrl-z` reaches the guest,
  and so does `kill <pid> 20` from inside the OS.
- A **builtin cannot be stopped**, for the same reason it cannot be interrupted: it is
  the shell, and the shell declines both signals. `sleep 30` typed at the prompt runs
  to completion.
- A job put in the background with `bg` **competes with the shell for the keyboard**
  if it reads standard input. Unix answers this with `SIGTTIN`, which stops a
  background process that tries to read the terminal; there is no `SIGTTIN` here yet.
- **Ctrl-Z at an idle prompt discards the line being typed**, exactly as Ctrl-C does.
  The shell ignores the signal, but the read it is blocked in is still cut short and
  cannot tell which signal cut it.
- **`wait` will not wait for a stopped job.** It says so and stops rather than blocking
  on something that can never finish - the shell ignores Ctrl-C, so there would be no
  way out of it.
- `SIG_CONT` is **not delivered to user space**. It is acted on where it is sent,
  because a stopped process never reaches a delivery point of its own.

## [0.8.0-alpha] - 2026-08-22

The terminal starts taking orders, and a process starts belonging to something. Two
halves of the same idea: a full-screen program needs to say where to draw, and the
user needs to be able to stop what they just started - which is never one process.

### Added

- **ANSI escape sequences.** Cursor positioning and relative motion, erase display and
  line, colour and attributes, a saved cursor, a scroll region, and line insert and
  delete. Enough for a full-screen program to draw with; anything else is swallowed
  rather than printed, because a sequence the terminal does not implement should leave
  no trace instead of spraying its parameters across the screen.

  Rows are counted in the 24 the screen actually shows. There are three coordinate
  spaces in the driver - the 25 rows the hardware has, the 24 of text because row 0 is
  the status bar, and the 100 the scrollback buffer holds - and an escape names rows in
  the second while the cursor is stored in the third.

- **Process groups.** A task belongs to one, named by the pid of the task that founded
  it, and inherited from its creator alongside uid and working directory. The terminal
  now points at a group rather than at a process: `SETPGID` (60) places a process,
  `TCSETPGRP` (61) hands the terminal over, `GETPGID` (62) reads it back.

  Both restricted deliberately. A caller may place itself or a child and nothing else,
  and may hand the terminal only to its own group or one holding a child - otherwise a
  background job could take the terminal from the shell that started it, and the next
  interrupt would reach something the user was not looking at.

- **Ctrl-C.** `SIG_INT` goes to every process in the foreground group. The shell puts
  each pipeline in a group of its own and hands it the terminal, so `ls | grep etc` is
  three tasks the user can stop with one key, and takes the terminal back when the job
  is done. Background jobs get their own group and are never given the terminal, which
  is what keeps the interrupt away from them.

  The shell ignores `SIG_INT` itself. Between commands the foreground group is the
  shell's own - there is nothing else to hand the terminal to - and a shell taking the
  default action would end the session the first time somebody pressed Ctrl-C at an
  idle prompt.

### Changed

- **The keyboard stops delivering Ctrl-C as data.** The driver has folded Ctrl-letter
  combinations into control bytes since v0.5.3, so `0x03` has been arriving in the
  input ring all along and being handed to whatever happened to be reading. What was
  missing was never the key: interrupting one process is not what Ctrl-C means, and
  until a process could belong to a group there was no way to name everything the user
  had started with a single command.

- **The terminal changes hands when a group empties, not when a task dies.** Reaping
  the foreground *task* used to hand the terminal on, which was the same thing only
  while a group could not have two members - the first stage of a pipeline exiting
  would have taken the terminal from the stage still running.

- **The view follows the cursor only when the cursor leaves it.** `view_offset` was
  recomputed from the cursor on every character, which works while the cursor can only
  move forward and becomes impossible once an escape can put it anywhere: positioning
  to the top of the screen and printing one character would have snapped the view down
  by twenty-three rows, so absolute addressing could never have worked. Sequential
  output is unchanged.

- **`jobs` numbers are stable.** It printed the table position, which renumbers every
  time an earlier job is collected - so the name of a job changed under the user
  between one listing and the next. A job is also a group now rather than a pid,
  because that is what the terminal addresses.

## [0.7.2-alpha] - 2026-08-21

A log record becomes a record. v0.6.1 made the ring wrap and stopped `printk()` feeding
it, so it stopped being a transcript of the screen — but a record was still just a line
of text, and every question about one was a question about parsing. When did this
happen. How many did we lose when it wrapped. Show me only the errors. None of them
could be answered, so none of them were asked.

### Added

- **A record ring.** 512 structured records against the 8 KB of flat text that held
  roughly 130 lines, and each carries its own level, module, monotonic timestamp and
  sequence number rather than a seven-character prefix on a string. About 88 KB of
  kernel data, which on a 128 MB machine buys a great deal for very little.

  The timestamp is a tick count, not a wall-clock time. It is monotonic and stays
  correct when the RTC is not, which is the property a log needs, and it is rendered to
  hundredths because `TIMER_HZ` is 100 — printing the six decimals Linux does would be
  printing precision this clock does not have.

- **Sequence numbers, and the count of what was dropped.** The byte ring wrapped
  mid-line and counted nothing, so a gap in the log looked like a quiet period. `dmesg`
  reports the count on descriptor 2, not 1: it is a note about the log rather than part
  of it, and putting it on stdout would drop it into the middle of `dmesg > boot.log`.

- **`KLOG_CTL` (59).** The severity threshold had sat at INFO since boot with nothing
  able to move it, so every DEBUG record the kernel composed was discarded unseen — a
  filter nobody can adjust is a filter that only ever removes. Clearing the log and
  moving the threshold change what everyone else sees and are root's; reading the
  threshold and the counters change nothing and are anyone's, which is what lets an
  ordinary program notice records went missing between two reads.

- **`dmesg -c`, `-n` and `-l`.** `-n` sets the kernel's threshold and prints nothing;
  `-l` chooses what to show out of what was already recorded. Deliberately not the same
  flag: running them together would make `dmesg -n debug` look as though it had lost
  the log. `-l` filters in the shell, as it does on Linux, so the syscall surface stays
  as small as it was.

- **`/dev/kmsg`.** Records in the structured form — `level,seq,ticks,flag;module: text`
  — so a reader wanting only errors reads the first field instead of parsing a prefix
  out of a line.

  Writable by root, which is what Linux's permissions on this device amount to, and
  rate limited. A record a program wrote is marked as one and carries its author's uid,
  so nothing a program writes can be mistaken for something the kernel said. Kernel
  records are deliberately *not* rate limited: a storm of errors is exactly when they
  matter, and suppressing them to protect the ring would throw away the evidence to
  preserve the container.

### Changed

- **The `DMESG` index counts records, not bytes.** A byte position cannot survive a
  record being dropped between two reads — every byte after the gap shifts and the
  reader is handed a torn line. Index 0 is the oldest record still held.

- **`klog_write_char()` is gone.** Feeding the ring one character at a time is how the
  old log came to hold the boot banner, and a ring of records has no meaning for half a
  record. The test that used it to force a wrap emits records instead, which is a
  better test of the same thing.

- **A log record's uid is as wide as a process's.** It was written as a `uint8_t`,
  which looked like plenty next to a handful of accounts and would have recorded this
  system's own `esduman`, uid 1000, as uid 232 — a record attributing itself to a user
  who does not exist. Caught by a compiler warning before it ever ran.

## [0.7.1-alpha] - 2026-08-21

Programs can ask for memory. Until now one had its ELF segments and a fixed 32-page
stack, decided by the loader and never changed again, and every tool in `/bin` worked
from arrays sized at compile time because there was nothing else to work from.

### Added

- **`brk` (56).** Moves a single boundary upwards from the end of the program's image.
  Raw kernel semantics rather than the libc wrapper's: the *resulting* break comes back
  whether or not it is the one that was asked for, so a caller finds out it failed by
  comparing. That also makes `brk(0)` the way to read the current break without moving
  it — zero can never be granted — which is why there is no second syscall for it.

  The loader plants the starting point from the highest address any `PT_LOAD` segment
  reaches, rounded up to a page. A task with no ELF image behind it — the idle task, or
  anything the test suite creates by hand — has no break and is refused rather than
  given one at whatever address its address space happened to be empty.

- **`mmap` (57) and `munmap` (58).** Anonymous, private, zero-filled pages in a region
  below the stack guard page, released independently of anything else. This is the
  primitive for one large buffer that outlives the allocations around it, which is what
  a text editor needs and what the break cannot give: a run can only be returned from
  the top, and the top is rarely the part a program has finished with.

  `munmap` refuses any range outside that region, and that check is the load-bearing
  one. Without it this is an arbitrary unmap: a program could pass its own stack, its
  own text, or the heap its allocator is standing on, and every address involved would
  be legitimately its own — nothing further down would object, and the fault would
  arrive somewhere else entirely.

- **`include/umalloc.h`.** A header-only allocator over both. Every program in
  `apps/bin` is a single translation unit compiled with `-nostdlib`, so there is no
  user-space libc to put an allocator in and no link step that would find one; each
  program takes a private copy by including it, which is the bargain they already make
  with their string helpers. It brings its own syscall wrapper rather than calling the
  program's, so that including it carries no ordering requirement.

  Small requests come off the break and freed blocks are reused, merged with whichever
  neighbours are also free. Requests of 64 KB or more get their own mapping, which
  `ufree()` hands straight back to the kernel.

- **Neither region keeps a list of what it has handed out.** The page tables already
  record exactly that, they are already walked by `fork()` and by process teardown, and
  a second record of the same facts is a second record to keep in step. `mmap` walks to
  find a free run instead of looking one up, which on a machine with sixteen processes
  is not a cost worth a subsystem.

### Changed

- **`ls` reads through the heap.** Its listing buffer was 1024 bytes on the stack, and
  `SYSCALL_READDIR` has always been willing to return 4096 — the listing simply stopped
  at an entry boundary with nothing to say that it had. The buffer is now allocated, and
  sized from the kernel's own ceiling rather than from what would fit in a stack frame.

- **`USER_STACK_TOP` was wrong and unused.** It read `0xBFFFF000` while the ELF loader
  built the stack at `0xB0000000` with its own literal, so the one constant naming the
  boundary was the only thing that disagreed with everyone else. It is the real figure
  now, and the loader reads it — which matters because the mmap region is laid out
  directly beneath the guard page and cannot be positioned from a number that is wrong.

- **Every page handed to user space is zeroed first.** A frame the allocator has just
  returned holds whatever its last owner left in it, so this is a security property
  rather than a courtesy: an unzeroed heap page is another process's memory delivered
  to this one. The pages go through `copy_to_user()` for the same reason the ELF
  loader's do — SMAP forbids a supervisor write to a user page outside that window.

## [0.7.0-alpha] "Cleave" - 2026-08-20

`fork()` stops copying. A child now gets the parent's pages themselves, both sides
give up write access, and the first write from either of them splits the page it
touched. The word cuts both ways on purpose: the pages cling together until
somebody writes, and that write cleaves them apart.

### Added

- **Reference counting in the physical allocator.** A bit answered "is this frame in
  use"; nothing could answer "by how many". One byte per frame — 32 KB at 128 MB of
  RAM — now sits behind the bitmap, so two address spaces can hold the same page and
  let go of it independently. `pmm_free_frame()` drops one owner and releases the
  frame at the last; every existing caller kept working unchanged, which is why the
  count lives there rather than beside it.

  The table is counted into the memory the allocator marks as the kernel's. Anything
  left out of that sum is memory the allocator considers free and will hand out, and
  a reference table handed out is one that gets overwritten by whoever received it.

- **Copy-on-write.** A fork used to duplicate every mapped page of the parent —
  including a 32-page user stack — and the overwhelmingly common next call is
  `exec()`, which throws all of it away. Sharing costs a page directory, the page
  tables under it and the child's `process_t`; on the test payload that is the
  difference between roughly 24 KB and roughly 170 KB per fork.

  Only a page that was writable becomes copy-on-write. One that was already
  read-only — a program's text — is shared exactly as it stands, because writing to
  it is an access violation in the child for the same reason it is in the parent,
  and marking it would have turned that into a silent private copy.

- **`meminfo` reports what is shared.** "Free" stopped being a complete answer the
  moment fork stopped spending memory: how much of what is in use is held by more
  than one address space is not derivable from any other figure on that line.

### Fixed

- **The kernel heap ignored a failed mapping.** `heap_grow()` took frames one at a
  time and mapped each one, dropping the result. `map_page()` fails when the page
  table for a directory entry cannot be allocated — the same exhaustion the free
  memory check above it watches for, one level down — and the loop carried on: the
  heap end advanced over an address with nothing behind it, and the block header was
  written into the hole. A kernel page fault, which is a panic, and it surfaced at
  whatever allocated next rather than at the growth that caused it.

- **The ELF loader leaked a frame whenever a mapping failed.** Allocation, mapping
  and zeroing shared one `||` chain, so a frame that was allocated and then failed to
  map belonged to nobody: not in the directory, so the teardown on the failure path
  could not find it, and never handed back. One frame per failed `exec()`,
  permanently. The chain also collapsed three distinct failures into one message
  naming only the middle one.

- **`kfree()` merged blocks that were neighbours in the list but not in memory.** The
  block list is kept in address order, which invites the assumption that consecutive
  entries are contiguous; the shrink path can leave a few dozen bytes between them.
  Merging across such a gap does not corrupt anything — the merged size comes out
  short of the real span, so the hole is lost rather than handed out — but the size
  stops describing the block, and every later split inherits that. `heap_grow()` had
  always made this check before merging onto the tail. Now both sites do.

### Changed

- **Two paths that a shared page would have broken silently.** Neither was reachable
  from the kernel-side test modules, and both would have failed every program on the
  system.

  `validate_user_writable_pointer()` decides whether a destination is writable by
  reading the page table entry's read/write bit — and after a fork that bit is clear
  on every writable page either side owns. `wait()`, `getcwd()` and every other
  syscall that answers into user memory would have returned `E_FAULT` on pointers
  that were never invalid. A page marked copy-on-write now counts as writable there:
  the write faults, the fault hands over a private copy, and the instruction retries.

  The other is ordering. A syscall writing into a shared buffer faults in *kernel*
  mode, because CR0.WP makes a read-only entry apply to Ring 0 as well. The page
  fault handler checked for an in-progress user copy first and would have sent the
  fault to that copy's fixup label, failing the syscall. The copy-on-write check now
  comes before it.

  A third followed from the second: resolving a fault inside a copy runs another
  copy, whose exit cleared the fixup label the interrupted one was relying on. A
  buffer inside a single page survived that; one reaching into a second shared page
  faulted again, found nothing registered, and would have taken the kernel down.
  Copies made from inside a fault handler now save and restore that state.

- **The FPU stays eager, and this is now a decision rather than an omission.**
  Switching lazily — parking the state behind CR0.TS and restoring it on the first
  use — was on the roadmap as an optimisation. It is the mechanism behind LazyFP
  (CVE-2018-3665), which leaks FPU and SSE register contents across processes
  speculatively, and every major operating system moved back to eager switching in
  2018. Exception 7 also lands in the general panic path here, so enabling TS would
  bring the kernel down on the first floating-point instruction. The cost of staying
  eager is one `fxsave` per context switch across at most 16 tasks.

## [0.6.1-alpha] - 2026-08-16

The log becomes a log: a record of events that wraps, rather than a transcript of the
screen that fills up and stops. And it survives the machine.

### Fixed

- **The log was not a ring buffer**, despite this source and the README both calling it
  one. It filled once to 8 KB and then silently dropped every record after — so `dmesg`
  showed the oldest part of the boot and nothing that had happened since. A log that
  discards the newest records is the opposite of a log. Nothing had noticed because
  nothing had ever filled it on purpose.
- **`printk()` fed every character it printed into that buffer**, so the boot banner, the
  ASCII art and the first-boot password prompts competed for the space with actual
  records — and were what filled it. A log is a record of events, not a transcript of the
  screen. `printk()` no longer feeds it; `klog()` composes a line and records that.

  The boot milestones still print their green `[OK]` list, which is boot UI, *and* appear
  in `dmesg` as records with a level and a module. That needs an entry point that records
  without printing, which is exactly what the split between the two implies.
- **`klog_int()` and `klog_hex()` put their value on the line after the message.** Both
  called `klog()`, which had already ended the line, so every value in the log sat
  orphaned beneath the text it belonged to. The value is a tail on the same line now.
- **A log message containing a `%` was read as a format string.** `klog()` passed the
  message to `printk()` as the format, which is a way to print whatever happened to be
  next on the stack.

### Added

- **`/var/log/kern.log`.** The directory has existed since the FHS hierarchy was created
  and has been empty ever since; the log lived in RAM and went with the machine. It is
  written at `sync`, `halt` and `reboot` — before the block cache is flushed, so the
  sectors go out with everything else.

  Written whole rather than appended to, because the format cannot append: a file is one
  AES-CBC blob authenticated over its entire plaintext, so adding a line means rewriting
  all of it. That is also why it is written at checkpoints rather than per record. The
  ring is snapshotted into a contiguous buffer first — it is not contiguous once it has
  wrapped, and the snapshot keeps the records this write itself produces from chasing
  their own tail into the file.

  Failure is reported and not fatal. A machine that will not halt because it could not
  save its log would be a worse bargain than a lost log — but it is *reported*: the errno
  used to be returned to three callers that all dropped it, so a checkpoint that could not
  write left the file simply absent with nothing to say why.
- **`sync` is a shell command.** `SYSCALL_SYNC` has existed since v0.4.x and nothing in
  user space had ever called it — it sat in the syscall table and in the README's
  reference, reachable from nowhere. That did not matter while it only flushed the block
  cache. It does now that it is also when the log is written, because the other two
  moments that write it are `halt` and `reboot`, and neither leaves a session to look at
  the result in.

### Changed

- `klog_write_char()` and `dump_klog()` are declared in `klog.h` rather than `kernel.h`,
  which pulls in twenty-two other headers. `KLOG_BUF_SIZE` joins them: the ring's size is
  part of its contract now that a reader cannot see past it.

### Known issues

`meminfo`, `hexdump`, `stack` and `/bin/free` still print from inside the kernel and
cannot be piped or redirected. They are root-only diagnostics and each needs a formatter
of its own.

## [0.6.0-alpha] - 2026-08-16

The clock becomes something a program can read, and stops being wrong on the last day of
a month.

### Added

- **`TIME` (55) reports the current wall-clock time.** The RTC was readable from Ring 0
  only — it drew the status bar and nothing else — so `date` printed a string compiled
  into its own binary, the same one on every boot in every year.

  The syscall fills an `esd_time_t` (`include/esdtime.h`), shared verbatim with user space
  the way `esd_stat_t` is, and laid out with no padding holes so `copy_to_user()` cannot
  hand over a byte of kernel stack. The fields are broken down rather than a count of
  seconds since an epoch: the RTC reports them that way, nothing here agrees on an epoch
  to count from, and both consumers in sight — `date` and the timestamps `/var/log` will
  want — need them broken down anyway.
- **`date` prints the actual date**, and **`date -u`** prints it in UTC. The shift between
  the two is done by the kernel rather than in `date`: moving a time between zones means
  the calendar carry, and a second copy of that arithmetic in user space is exactly what
  this release exists to get right once.
- **`/etc/timezone` sets the offset at boot.** It was compiled in, so a machine in the
  wrong place had to rebuild the kernel to see the right time. The file holds a signed
  hour count and says so in its own header — an offset and not a zone name, because
  `Europe/Istanbul` is only meaningful with a timezone database to look it up in, and
  tzdata is measured in megabytes against a 2 MB disk.

  A missing or unparseable file leaves the compiled-in default in place rather than
  failing the boot, and an offset outside −12..+14 is refused: that range is what real
  zones occupy, and anything else is a misparse that would move the date by days. The
  default still matters for exactly one second — the first status bar is drawn before the
  filesystem is up.

### Fixed

- **The date was wrong on the last day of every month.** The timezone offset was applied
  as `hour += 3` with a day carry that never looked at how long the month was, so 21:00
  UTC on 31 August produced **32/08**, and 31 December produced 32/12 rather than 1
  January. The carry uses real month lengths now, with the full Gregorian leap rule —
  including the century cases, where 2100 is not a leap year and 2000 is.

  The offset moved into a named constant, and the arithmetic handles negative offsets as
  well as positive ones. Nothing uses a negative one today, which is exactly why it is
  written: leaving half of it out is a trap for whoever changes the constant.
- **The RTC could be read mid-update.** The update-in-progress flag was checked once and
  then seven registers were read one after another, and the chip is free to begin an
  update in the middle of that. At a second boundary the values straddle it; at a midnight
  boundary they produce a date that never existed. The registers are read twice and
  compared now — two readings that agree cannot straddle an update, because an update
  always changes at least the seconds.
- **The status bar changed its own label one second after boot.** `kernel_main()` drew it
  with the version string and the per-second refresh drew it with the literal
  `"esdumanOS"`, so the left half changed as soon as the clock first ticked. Both read one
  definition now, and it is the name: the version belongs in `/etc/os-release`, where a
  program can read it, rather than in a corner of the screen.
- **The year is printed in full.** The formatter emitted `"20"` followed by the RTC's two
  digits, so the century was a literal in the middle of a string.

### Known issues

The clock still has no way to be **set** — the RTC is read and never written, so a wrong
hardware clock stays wrong. The offset is configuration now, but there is no daylight
saving: this machine's zone has been permanent UTC+3 since 2016, so a fixed offset is
correct here rather than a shortcut, and a zone that still changes twice a year would
need a database this system has no room for.

The RTC is assumed to hold UTC, which is what QEMU presents by default. A machine whose
CMOS holds local time would need `/etc/timezone` set to 0 rather than its real offset.

`stat` still reports no timestamps, because the on-disk format carries none — that is
unchanged and documented where it always was.

## [0.5.4-alpha] - 2026-08-16

The parser stops guessing, and `/etc` stops being an empty directory.

### Fixed

- **A pipeline can have more than two stages, and `>` can be combined with `|`.** The
  parser took whichever of the two it met first and stopped, handing everything after it
  on as ordinary arguments: `a | b | c` ran `a` against the literal tokens `b | c`, and
  `a | b > f` passed `> f` to `b` as two words. Both were silent — the wrong thing ran and
  reported success.

  Up to four stages now, each forked, joined by one pipe per gap, and `>` belongs to the
  stage it appears in. Four is a process budget rather than a preference: an external
  stage costs two tasks, because the forked child runs the program through `exec()`, which
  creates a task of its own. Asking for a fifth is refused with a message. So is a bare or
  doubled `|`, and so is backgrounding a pipeline — that last one used to run in the
  foreground with the `&` quietly dropped.
- **`$VAR` and `~` are expanded in every stage.** Expansion ran after the split, and the
  split had already written a null over the `|`, so the loop stopped there and no token
  past the first stage was ever expanded.
- **Every `~` gets its own storage.** One shared static buffer served the whole line, so
  each expansion overwrote the last and every token ended up pointing at the same string:
  `cp ~/a ~/b` passed `~/b` twice and copied a file onto itself. Expansions come out of an
  arena that is reset per command, and running out of it is reported rather than absorbed.
- **`/bin/rm`, `/bin/mv` and `/bin/kill` are reachable.** All three shipped in the image
  and were unreachable by any spelling, because the builtin table was consulted first and
  each name is also a builtin. A word containing a slash is a path now and is never
  matched against that table, which is the rule every real shell uses.
- **`rm <directory>` no longer orphans what is inside it.** A child records its parent as
  the parent's index in the directory table, and deleting the parent cleared that slot and
  stopped. The children stayed behind pointing at an index that no longer described them —
  unreachable through any path, and visible again as somebody else's contents the moment
  the slot was reused. A directory that still holds something is refused with `ENOTEMPTY`;
  an empty one still goes, which makes this `rmdir(2)` rather than a refusal to remove
  directories at all.

### Added

- **`/etc` has system files in it.** The directory has existed since the FHS hierarchy was
  created and held nothing but the password database, so every fact a tool needed was
  compiled into it instead.

  `/etc/os-release` carries the version the kernel was built with, generated from the same
  macro the status bar uses, so the two cannot drift. `/etc/hostname` is what the prompt
  now reads — the name was a string literal in two places. `/etc/motd` is printed at
  startup. `/etc/profile` is read by the shell before its first prompt: only
  `export KEY VALUE` is recognised, and the file says so in its own opening lines. It is a
  settings file rather than a script, because running arbitrary commands from it would
  mean forking and exec'ing before a prompt appears, and a syntax error in it would be a
  shell that will not start.

  None of the four is required. Each falls back to what the shell already had, so a disk
  image made before this release still boots.

### Known issues

These files are written in the same block as the `/bin` tools, which runs only when
`init.elf` is absent from the disk — so a disk image carried over from an earlier version
keeps its old `/etc` until it is recreated. `make run` clears the disk; `make run-dev` and
`make restart` need `make reset-disk`.

## [0.5.3-alpha] - 2026-08-16

Both ends of a pipeline learn how to stop. The writer gets a death, the reader gets an
end, there are finally programs willing to sit at the far end of a pipe — and `ls` and
`dmesg` stop writing past it to the screen.

### Added

- **`SIGPIPE` (13).** A process that writes to a pipe with no readers left is signalled,
  and the default action terminates it with status 141. `pipe_write()` has refused that
  write since v0.5.2, but a refusal is only a return value and nothing in user space reads
  one — `printk()` discards it and so does every `/bin` tool — so the stage ran to the end
  of its input with every write failing in silence. This is what actually stops it.

  Raised only for writers that arrived from Ring 3. The kernel test modules drive `write`
  through `int 0x80` as the task running the suite, and signalling that task would end the
  run — which would look like a passing one, since an interrupted run still prints
  everything it got through.
- **`SIG_IGN`.** A process can now decline a signal, which is a third state
  `signal_handlers[]` did not have: it held an address or 0 for the default. The
  disposition is stored as the sentinel 1, the value POSIX uses, so inheritance and reset
  come for free — `fork()` already copies the array and a new program image already starts
  with it cleared.

  The shell declines `SIGPIPE` for itself: losing it would end the session, and there is
  no way to get another one. Because a disposition survives `fork()`, every stage the
  shell forks would start out declining it too — which is the exact runaway this release
  exists to stop — so each forked child restores the default before running anything.

  `SIGNAL_REG` now reports what it decided: `E_OK`, `E_FAULT` for a handler address user
  space cannot execute, or `E_INVAL` for a signal number out of range. It used to answer 0
  in every case, including the ones it had refused. The check itself was written out twice
  — once in the syscall and once in `register_user_signal()` — and only one copy was
  relaxed for `SIG_IGN`, so Ring 3 callers were turned away with `E_FAULT` while
  kernel-mode callers of the same function succeeded. There is one copy now.
- **Ctrl-D ends console input.** `sys_read()` on the console either handed back a byte or
  blocked; there was no path that returned 0, so a program reading standard input from a
  terminal could never finish. That was survivable while nothing read standard input. It
  stops being survivable now, because there is one terminal, no Ctrl-C and no job control,
  so a read that cannot end takes the machine with it.

  Ctrl was the one modifier the keyboard driver never tracked. It now folds a letter to
  its control code, which is the ASCII rule, so Ctrl-D produces the end-of-file byte and
  Ctrl-C has a path waiting for it once there are process groups to send it to. Ctrl with
  anything other than a letter is passed through unchanged.
- **`/bin/wc`** counts lines, words and bytes, and reads standard input when no file is
  named. Its output is three numbers however much it consumed, which makes `something | wc`
  the cheapest way to see that a stage reached the end of its input rather than stopping
  early.

### Fixed

- **`ls` and `dmesg` output goes through the process's standard output.** Both produced
  their listing inside the kernel with `terminal_putchar()`, which knows nothing about the
  calling process — so the text went to the screen whatever descriptor 1 pointed at.
  `ls | grep bin` read an empty pipe, `dmesg | head` fed an empty pipe, and `ls > names`
  created an empty file. Redirection has been wired up since v0.4.3 and this was never
  noticed, because until v0.5.2 a pipeline could not run its stages concurrently and until
  this release nothing consumed one.

  `ls` is now built on `READDIR` (44), which already handed entries back in a buffer and
  which tab completion already used; the shell prints them itself. `DMESG` (39) grew a
  buffer form — `dmesg(buf, size, offset)` copies a slice and returns the count — and the
  shell loops over it. Passing a null buffer still dumps to the screen, which is what a
  caller with no descriptors of its own wants.

  The loop is in the shell rather than the kernel on purpose. An 8 KB log does not fit a
  4 KB pipe, so the write blocks, and a blocked syscall resumes by re-running from its
  `int 0x80` — a kernel-side dump that blocked halfway would start over and emit
  everything twice. With the offset held in the caller, each write blocks and restarts
  harmlessly.

  `meminfo`, `hexdump`, `stack` and `/bin/free` still print from the kernel and still
  cannot be piped or redirected. They are root-only diagnostics and each needs its own
  formatter; recorded under Known Limitations rather than fixed here.
- **`ls <directory>` reads its argument.** It passed the id of `.` whatever was typed
  after it, so `ls /bin` listed the working directory — and looked convincingly like
  `/bin` was empty.

  The listing loses its colour in the move: the kernel set it per entry and the shell has
  no syscall for it. The `[DIR]` and `[FILE]` markers carry the same distinction in text,
  and colour codes written into a pipe or a file would have been wrong anyway.
- **`grep` and `head` read standard input when no file is named.** Both opened a file by
  name and nothing else, so a pipeline could be parsed, forked and connected with `cat` as
  the only program in the system willing to consume one. `echo x | grep x` did not work.
- **`grep` no longer stops at the first 511 bytes of a file.** It issued a single read of
  511 bytes and searched what came back, so a match on line 40 of a 2 KB file was simply
  not found — silently, with an exit status of 0. It now assembles lines as bytes arrive,
  which is the same loop the standard-input path needs, so the two became one. A single
  line longer than 256 bytes is truncated rather than split; splitting would report one
  line as two and could match across a boundary that is not in the input.

### Deliberately not done

- **Ctrl-D at the shell prompt does not exit the shell.** The byte is dropped by the input
  loop, which accepts only printable characters, and the read returns 0 once before
  blocking again — no busy loop, no effect. Making it exit is a behaviour change rather
  than part of this one.
- **`grep` still does not distinguish "no lines matched" from "lines matched"** in its
  exit status. Recorded since v0.4.x and still true; changing it would alter what `&&` and
  `||` do with a `grep`.

### Documentation

The README's Known Limitations and Roadmap sections were reconciled — for v0.5.2 as well
as this release. The v0.5.2 documentation commit said it retired the pipeline deadlock and
the absence of job control, but touched only this file, so the README went a release
claiming the shell did not use `fork()` yet, that a pipeline could deadlock, and that
there was no `&` or `jobs`. All three had been false since v0.5.2.

## [0.5.2-alpha] - 2026-08-15

The shell runs on `fork`. The pipeline deadlock is gone, and `kill` can be tried by hand
for the first time.

### Fixed

- **`cmd1 | cmd2` no longer deadlocks.** The shell executed the first stage to completion
  with stdout pointing at the pipe, and only then started the second to drain it. Nothing
  was reading while the first stage wrote, so a first stage producing more than the 4 KB
  the pipe holds blocked with no reader and never resumed — taking the shell with it.
  Both stages are forked now and run at once. This is what `fork()` was added for.

  The shell closes both pipe ends before waiting, which is load-bearing rather than
  tidiness: the reader sees end-of-file only when every write end is shut, and the shell
  holds one.
- **A pipe with no readers accepted data.** `pipe_write()` checked for a departed reader
  only inside the buffer-full branch, so as long as there was room the write succeeded and
  reported the byte count — bytes handed to a pipe nobody would ever read, and a caller
  told they had been written. The condition surfaced only once 4 KB had accumulated.

  Unreachable until this release: with the stages run one after the other, the reader was
  always started after the writer had already finished. Concurrency made it the ordinary
  case. The check now runs first, whatever room is left.

  The warning is logged once per pipe rather than once per rejected write. A writer that
  does not check its write results — `printk()` does not — keeps going until its input is
  exhausted, and a line per attempt buried everything else in the log.
- **`cat` with no file argument reads standard input.** It was an error, which left the
  shell with pipes and nothing able to read one: `grep` and `head` both open a file by
  name, and so did this. `a | b` could be parsed, forked and connected, and there was no
  `b` that would take it. Descriptor 0 is never closed on that path — it belongs to
  whoever started the process, and a builtin closing it would leave the shell without
  input.

### Added

- **Background jobs.** A trailing `&` runs the command in a child and returns the prompt
  immediately. Until now there was no way to hold a prompt while another process ran —
  which is why `kill` went five releases without anyone noticing it did nothing: there
  was never a live target and a prompt at the same time.
- **`jobs`** lists what this shell started and has not yet collected, and **`wait`**
  blocks until all of them have finished. Finished jobs are reported above the next
  prompt rather than the moment they report, so a job ending mid-line does not print over
  what is being typed.

### Changed

- **`wait()` takes the POSIX shape: it returns the pid and writes the status through a
  pointer.** It shipped in v0.5.0 returning the status directly, which is not enough for
  the first thing that needed it — a shell forking two pipeline stages gets two statuses
  back and has to know which is which, because the pipeline's own status is the last
  stage's. The first real consumer is where an API of this kind gets to be wrong, so it
  was changed while there was exactly one.

  A non-zero third argument asks it not to block. The three answers are distinct: a pid
  means a child reported, zero means children exist but none has, and `E_CHILD` means
  there are none at all. Collapsing the middle two would leave a shell either blocking on
  a running job or forgetting one it still has.

  Delivery changed with it. `exec()` still has its status written straight into its saved
  frame — it returns the status itself and cannot re-run without launching the program a
  second time. `wait()` has to write into the caller's memory, which `reap_task()` cannot
  reach from another address space, so its status is parked and the syscall is restarted
  to collect it with the right directory live. The two are told apart by a new wait
  reason.

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
