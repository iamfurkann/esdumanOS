<div align="center">

# esdumanOS

**A 32-bit x86 operating system kernel written from scratch in C and assembly.**

[![CI](https://github.com/iamfurkann/esdumanOS/actions/workflows/ci.yml/badge.svg)](https://github.com/iamfurkann/esdumanOS/actions/workflows/ci.yml)
![Version](https://img.shields.io/badge/version-1.2.0--beta.1-blue)
![Architecture](https://img.shields.io/badge/arch-x86__32-orange)
![Language](https://img.shields.io/badge/language-C%20%7C%20x86%20ASM-green)
[![License: MIT](https://img.shields.io/badge/license-MIT-purple)](LICENSE)
![Status](https://img.shields.io/badge/status-beta-yellow)
[![Website](https://img.shields.io/badge/Website-Live-2ea44f)](https://iamfurkann.github.io/esdumanOS-website/)

*An independent operating system, booting through GRUB via Multiboot,*
*with preemptive multitasking, an encrypted file system, and a Unix-style shell.*

</div>

> **⚠️ Beta Notice:** This is a development release intended for testing and educational
> purposes only. It is not suitable for production use. Expect bugs, crashes, and
> incomplete features.
>
> **What 1.0 means here:** the system call interface is frozen. No number already
> assigned changes its value or its meaning, the retired numbers are never reused, and
> new calls continue from the highest assigned number — `SETKEY` took 68 in v1.1.0 and
> the next one takes 69. Enforced by `tests/kernel/test_abi.c`, which asserts every
> number, errno, flag and security level by literal value. It is a promise about the
> interface, not a claim about the system underneath it: the
> [Known Limitations](#known-limitations) are the same list they were, one test module is
> not deterministic, and there is still no `mount`. That is what the `-beta.1` is for.

---

## Table of Contents

- [Overview](#overview)
- [Current Status](#current-status)
- [Features](#features)
- [Architecture](#architecture)
- [Quick Start](#quick-start)
- [Building](#building)
- [Running](#running)
- [Testing](#testing)
- [Project Layout](#project-layout)
- [System Call Reference](#system-call-reference)
- [Security Model](#security-model)
- [Known Limitations](#known-limitations)
- [Roadmap](#roadmap)
- [Contributing](#contributing)
- [License](#license)
- [Credits](#credits)

---

## Overview

esdumanOS is a from-scratch operating system kernel for the x86 (IA-32) architecture. It does not derive from Linux, BSD, or any existing kernel codebase. Every subsystem, from the bootloader handoff through memory management, process scheduling, file system operations, and user authentication, is written specifically for this project.

The kernel boots via GRUB using the Multiboot specification, transitions through Protected Mode with full GDT/IDT/TSS initialization, sets up paged virtual memory, and launches a preemptive multitasking environment with Ring 0 / Ring 3 separation. User-space programs are loaded from ELF binaries, and the system provides a Unix-inspired shell with pipes, output redirection, conditional chaining, and environment variables.

A central design goal is treating security as a first-class concern rather than an afterthought. The kernel includes a tiered security level system, AES-256-CBC disk encryption, user authentication against a shadow password database, and permission enforcement at the system call boundary.

---

## Current Status

**Version:** 1.2.0-beta.1

esdumanOS is in the **Beta** stage. The core kernel subsystems are functional and the
OS boots in QEMU. The privilege boundary is genuinely tested rather than merely
written: the self-test suite hands control to a real Ring 3 process and asserts from
CPL=3, and it also runs under hardware SMEP/SMAP enforcement. Before v0.2.0 every
automated test ran at CPL=0, so process isolation and the scheduler were never
exercised at all.

v0.3.0 moved the working directory into the kernel. Until then the shell kept it in a
userspace global and handed a directory id to every syscall that touched a path, which
meant a process chose where its own relative lookups started — and every `/bin` tool
passed a hardcoded 0, so they all operated on the root directory no matter where you
had `cd`'d to.

v0.4.0 lets a program ask about things rather than only do them: `stat`, `fstat`,
`lseek`, `getpid` and `sleep`. Two of those needed something the tree did not have.
`stat` had to answer with the size a `read()` would return, which is not the size the
directory table records — with encryption on by default, the stored form carries an IV,
a header and padding — so sizes now come from a helper that reads the real length out of
the file's header. And `sleep` is the first production use of `WAIT_TIMER`, which had
been defined since the beginning and referenced only by a test.

v0.4.2 is a stability patch rather than a feature release. The whole v0.4.x tree was
audited before starting work on `fork`/`wait`, and the audit found more than the
roadmap expected: a segfaulting user program left its parent blocked forever and leaked
its entire address space, closing a pipe never woke the process blocked on it, and the
shell overran its own stack on a line of 33 short words. Those and a dozen more are
fixed; the release notes list what is still known-broken rather than quietly carrying
it.

v0.4.3 gives the kernel the ability to write to a file through a descriptor, which it
had never had — `sys_write` handled the console, pipes and `/dev` nodes and dropped
everything else. That single gap was why `/bin/cp` produced empty files and why the
shell's `>` could not be connected to anything. Both work now, within semantics the
disk format dictates: a write is buffered and committed whole when the last descriptor
closes, so there is no appending and no writing into the middle of a file.

v0.4.4 is a header tidy-up ahead of `fork`/`wait`, with no behaviour change. It found
three real defects hiding in declarations: `timer_ticks` was declared without the
`volatile` its definition carries, three declarations in `rtc.h` sat outside the include
guard, and four of the fifteen embedded-ELF lengths were declared with a type that
disagreed with their definitions. The first survived because `timer.c` included none of
the headers that declare what it defines — so nothing ever compared the two.

v0.4.5 splits process teardown apart ahead of `fork`/`wait`. Ending a task and switching
away from it were one function, so the only way to end a task was to *be* it — which is
why `kill` could not kill: a signal to a process with no handler registered was recorded
and then dropped, and every process has no handler registered by default. `SIG_KILL` and
`SIG_TERM` now terminate a target that has not handled them. The same release stops a new
process control block carrying whatever the heap last held, and stops a pid in use from
being issued twice; both are fields `fork()` would have copied.

v0.4.6 touches no kernel code. It makes `make fuzz` explain itself: libFuzzer's runtime
uses the `POPCNT` instruction, and on a CPU without it the target died with `SIGILL`
inside the sanitizer, reported only as "deadly signal" and with a stack trace pointing at
the one place the fault was not. That is reachable whenever the host is an emulated x86 —
QEMU's default CPU model omits the instruction — so it went unseen until development moved
onto Apple Silicon. The same release pins CI to a fixed runner image instead of the
floating `ubuntu-latest`, so a toolchain upgrade is something chosen rather than
discovered.

v0.5.0 adds `fork()` and `wait()`. Until now every process in this system came from an
ELF file: `exec` built an address space and filled it from disk, and the caller blocked
until the result exited. That is enough to run a program and not enough to run two, which
is why the shell executes `cmd1 | cmd2` one stage at a time and deadlocks when the first
stage outgrows the pipe buffer. A process can now be made from a process — the child gets
a private copy of its parent's memory, descriptors, signal handlers and registers, and
returns from the call as if it had made it itself. The shell does not use any of this
yet; that is the next release, and it is deliberately separate so that `fork` is verified
before anything is built on it.

v0.5.1 changes no kernel code at all. Test and production builds compiled the same
sources with different flags into the same objects, and make cannot see a flag change —
so every test run began by deleting every object and paying for a full rebuild, and a
release image built without `make clean` first would quietly inherit the reduced PBKDF2
iteration count. Each flavour now has its own tree under `build/`, which removes the
hazard by construction and makes incremental builds work again: a one-file change takes
seconds instead of two minutes.

v0.5.2 puts the shell on `fork`. Both stages of `cmd1 | cmd2` now run at once, so a first
stage producing more than the 4 KB the pipe holds no longer blocks with no reader and
takes the shell down with it. A trailing `&` runs a command in the background, `jobs`
lists what is still running and `wait` blocks for all of it — which together mean there
is finally a way to hold a prompt while another process runs, and therefore a way to try
`kill` by hand. That absence is why `kill` went five releases without anyone noticing it
did nothing.

v0.5.3 finishes the pipeline by giving both of its ends a way to stop. A writer whose
reader has gone is now sent `SIGPIPE` and dies; refusing the write, which is all v0.5.2
did, was only a return value, and nothing in user space reads one — so the stage ran to
the end of its input with every write failing in silence. At the other end, `grep`, `head`
and the new `wc` read standard input when no file is named, which is what makes a pipeline
have somewhere to go: until now `cat` was the only program in the system that would take
one. Reading standard input from a terminal needed an end of its own, so Ctrl-D became a
real key — the console could not report end-of-file at all, and on a machine with one
terminal and no Ctrl-C, a read that cannot end takes the machine with it. Testing all of
that turned up one more thing: `ls` and `dmesg` printed from inside the kernel, straight
to the screen, so they had never gone through a pipe or a redirect in their lives.
Both now hand their output back and let the shell write it.

v0.5.4 stops the parser guessing. `a | b | c` used to run `a` against the literal tokens
`b | c`, and `a | b > f` handed `> f` to `b` as two words: the parser took whichever
operator it met first and passed the rest on as ordinary arguments, so the wrong thing ran
and reported success. A pipeline is up to four processes now, `>` belongs to the stage it
appears in, and the cases that are still not supported are refused with a message instead
of reinterpreted. Three programs in `/bin` — `rm`, `mv` and `kill` — became reachable in
the same change, having shipped in every image so far and been shadowed by builtins of the
same name. The other half of the release is that `/etc` finally holds something: the
hostname the prompt prints, the version the kernel was built with, a message of the day,
and a `profile` the shell reads before its first prompt.

v0.6.0 makes the clock something a program can read, and something a user can configure.
The offset was compiled in, so a machine in the wrong place had to rebuild the kernel;
`/etc/timezone` holds it now — a signed hour count rather than a zone name, because there
is no timezone database here and there will not be one on a 2 MB disk. `date -u` prints
UTC, with the shift done in the kernel so the calendar carry lives in one place. The clock
was reachable from Ring 0 only —
it drew the status bar and nothing else — so `date` printed a string compiled into its own
binary, the same one on every boot. `TIME` (55) hands the broken-down fields to user space,
and `date` uses them. Two defects went with it: the timezone offset was applied with a day
carry that never looked at how long the month was, so 21:00 UTC on 31 August produced
`32/08`; and the RTC's update-in-progress flag was checked once before seven registers
were read one after another, which lets a reading straddle an update and, at midnight,
produce a date that never existed.

v0.6.1 makes the log a log. It was a fill-once buffer that stopped accepting at 8 KB and
silently dropped every record after, while this file and its own source both called it a
ring buffer — so `dmesg` showed the oldest part of the boot and nothing since. What filled
it was `printk()`, which fed every character the kernel printed into it: the boot banner,
the ASCII art and the first-boot password prompts all competed for the space with actual
records. It wraps now, and holds records rather than a transcript of the screen.
`/var/log` also stops being an empty directory — the log is written to `kern.log` at
`sync`, `halt` and `reboot`.

v0.7.0 stops `fork()` from copying. A child used to receive a private duplicate of every
page its parent had mapped, made at the moment of the call — a 32-page user stack before
anything else — and the overwhelmingly common next call is `exec()`, which discards all of
it. The child now gets the parent's frames themselves, both sides give up write access,
and the first write from either of them splits the page it touched. What made the copy
necessary was the teardown path, which released every user frame it found unconditionally;
the physical allocator counts owners now, so two address spaces can hold a page and let go
of it independently. Three memory defects went with it, all of them latent: the kernel heap
advanced its end pointer over a mapping that had failed, so the next block header was
written into an unmapped hole; the ELF loader lost a frame on every failed `exec()`; and
`kfree()` merged blocks that were neighbours in its list without checking they were
neighbours in memory. Two paths would have broken silently under sharing and were found by
reading rather than by testing — the writable-pointer validator rejects a page whose
read/write bit is clear, which after a fork is every writable page on both sides, and the
page fault handler examined the in-progress-copy fixup before it examined the page.

v0.7.1 gives user-space programs memory they can ask for. Until now a program had
its ELF segments and a fixed 32-page stack, decided once by the loader, and every tool
in `/bin` worked from arrays sized at compile time - there was no allocator, and with
each program compiled as a single translation unit against `-nostdlib`, nowhere to put
one. Two syscalls now: `brk` moves a single boundary upwards from the end of the image,
and `mmap` hands out an independent run below the stack that `munmap` releases on its
own. `include/umalloc.h` is the allocator over both, in a header because there is no
link step that would find a library; small requests come off the break, and anything
from 64 KB up takes its own mapping so that freeing it really returns the pages. Both
kinds arrive zeroed, which is a security property rather than a courtesy - a frame the
allocator has just handed out holds whatever its last owner left in it. `ls` is the
first user: its listing buffer moved off the stack and grew to the 4 KB the kernel was
always willing to return, four times what it had been reading.

v0.7.2 makes a log record a record. v0.6.1 made the ring wrap and stopped `printk()`
feeding it, so it stopped being a transcript of the screen — but a record was still
just a line of text, and every question about one was a question about parsing.
When did this happen, how many did we lose when it wrapped, show me only the
errors: none could be answered, so none were asked. The ring holds structured
records now, 512 of them against the 8 KB of flat text that held roughly 130
lines, and each carries its own level, module, monotonic timestamp and sequence
number. The sequence numbers are what make a gap visible: `dmesg` reports how many
records went when the ring wrapped, which nothing could ask before. `KLOG_CTL`
gives user space the rest — the severity threshold had sat at INFO since boot with
nothing able to move it, so every DEBUG record the kernel composed was discarded
unseen. `dmesg` grew `-c`, `-n` and `-l` to reach it, and `/dev/kmsg` streams the
records in a machine-readable form, writable by root so that a program's words are
recorded as a program's rather than as the kernel's.

v0.8.0 gives a process something to belong to. The terminal used to point at a single
process, which is not what the user typed: `ls | grep etc` is three tasks and one
intention, and interrupting one of them is not interrupting the command. A task now
belongs to a process group, inherited from its creator alongside its uid and working
directory, and the terminal points at a group — so Ctrl-C reaches everything the user
started with one key. The keyboard had been folding Ctrl-C into a 0x03 since v0.5.3 and
handing it to whatever happened to be reading; what was missing was never the key, it
was somebody to send it to. The terminal driver learned ANSI escape sequences in the
same release, which nothing in user space emits yet — the editor two releases out is
what they are for.

v0.8.1 lets the user set a job aside instead of losing it. Stopping needs a state the
scheduler never had: a task that is neither running nor waiting for anything, holding
its memory, its descriptors and the instruction it was on until somebody asks for it
back. Ctrl-Z parks the foreground job, `fg` gives it the terminal again and `bg` lets it
run on without one, and a job is named by number — `%1`, or nothing at all for the most
recent. A stopped task remembers what it was doing, because almost every blocking
syscall here resumes on the trap instruction and re-evaluates what it was waiting for;
`exec()` is the exception, and a task stopped inside one goes back into exactly that
wait. `exec()` also gained a form that hands back a pid instead of blocking, which is
what lets the shell own a foreground command the way it already owned a pipeline —
without a pid there was no group to put it in, no way to hand it the terminal, and
nothing to name when the user stopped it.

v0.8.2 gives the keyboard the other half of what v0.8.0 gave the screen. The terminal
could be told where to draw and no key could tell a program where to move: the arrows
were bound to the scrollback and Home, End, Delete and the Page keys landed on zero
entries in the layout tables and were dropped without trace. They send the sequences a
terminal sends now, whole or not at all, and scrollback moves to Shift with the Page keys
because a key the driver consumes is a key no program will ever see. The shell is the
first user — the line grows a cursor, and the last eight commands come back with the up
arrow. Two more things an interactive program needs arrived with them: `POLL` (63), which
is the only honest way to tell the Escape key from the first byte of a sequence without a
timer, and `SIGTTIN`, which parks a background job that tries to read the terminal instead
of letting it race the shell for every keystroke.

v0.8.3 is what those were for. `/bin/edit` is the first program here to draw a full
screen, and it is the first consumer of every one of them: the escape sequences v0.8.0
taught the terminal, the memory v0.7.1 let a program ask for, the keys v0.8.2 gave the
keyboard, and the stop and continue v0.8.1 made a job survive. It is modal in the shape
`vi` has, which is a decision about this machine rather than about taste - there is no
Alt and nothing past F3, so a modeless editor would have to spend Ctrl-letter
combinations on its commands, and Ctrl-C, Ctrl-D and Ctrl-Z already belong to the
terminal. The arithmetic it runs on lives in a header so that the test suite can reach
it, because a program in `/bin` has no link step and logic outside a header is logic
nothing can assert about.

v0.8.4 finishes both halves of that. The editor got `u` and `/`, which is the difference
between something you can demonstrate and something you can write a file with: before it,
the only way out of a mistake was `:q!` and the loss of everything since the last write.
The undo log went into the same header the buffer lives in, so the suite can assert on it
— an undo that is wrong hands back a file that is not the one the user had, and no screen
can show you that. The shell, meanwhile, stopped drawing only the last row of a command
that wrapped past column 80. Neither needed anything from the kernel: the terminal has
had the sequences since v0.8.0, and what was missing was arithmetic on the other side of
the system call.

v0.9.0 "Vouch" makes the disk say what it is. Nothing on it ever did: the directory
table and the allocation table were read out of their sectors and used as they were,
with no magic number, no version and no check — so a kernel could not tell one of its
own images from an older one, from a disk belonging to something else, or from noise.
A superblock in sector 0 answers that now, and it carries the geometry too, so a later
layout change needs different values there rather than a new version of anything. The
format change is the one thing that cannot be taken back, so everything it was short of
went in together: permission bits, a group, creation and modification times, and entry
ids wide enough to stop capping the file system at 256 files. The room came from the
name field, which was 256 bytes of a 272-byte entry — at 64 it pays for all of it, for
twice as many entries, and still costs less memory than before. An image from an
earlier release is recognised and refused rather than read as though it were this one.

v0.9.1 makes the bits v0.9.0 stored decide. What granted or refused access until now
compared names — `"tmp"` at the root was writable by anybody, an entry called `"root"`
and owned by uid 0 was closed to everybody, `"shadow"` could not be read — so a file's
permissions were a property of what it was called, and renaming one changed who could
touch it. Access is now the Unix rule: search permission on every directory above the
entry, then read or write on the directory the operation happens in, with the matching
class deciding and no fallthrough to the next. `chmod` and `chown` arrived to set the
bits, `ls -l` to show them, and the system's own paths are put back to the permissions
they must have on every boot rather than only when they are created — because
everything v0.9.0 wrote took the default `0644`, `/etc/shadow` included.

v0.9.2 clears the last category on the list to 1.0: answers that looked right and were
not. `meminfo > mem.txt` produced an empty file and reported success; `cat_raw f | grep`
fed an empty pipe; `grep nothing f && echo hi` printed `hi`. The first two were the same
defect — the output was printed from inside the kernel and never reached the calling
process's descriptor 1 — and the fix is the one `dmesg` got in v0.8.x: the kernel renders
into a buffer the caller supplies, and the caller writes it. `cat_raw` needed a different
answer, because 64 KB of file is 192 KB of hex text and no buffer wants that; the syscall
became `READ_RAW`, which is `read()` against the stored form, and the formatting moved to
the shell where it belongs. `grep` now reports 0 for a match, 1 for none and 2 for a
failure. And the clock can be set, which it never could — it was readable and not
writable, so `date` showed whatever the machine came up with and nothing could correct
it, which started mattering the moment v0.9.0 began stamping files.

v0.9.3 finishes something v0.9.0 started. Widening an entry id from a byte to two was
mostly a matter of following the compiler, and the part the compiler cannot help with is
the explicit cast — `(uint8_t)` compiles cleanly and truncates silently. Eight of them
were still in the tree, on the paths people use most: `ls <dir>` and `cd` asked the
permission question about one directory and then operated in another, `pwd` rendered a
name belonging to some other entry, and the check that refuses to delete a directory that
still holds files stopped seeing the files. All of them need an entry id past 255 to
bite, which a table of 512
reaches, and none of them fail loudly when they do — they answer about a different
directory. The entropy pool also stops starting from scratch: a 32-byte seed is carried
in `/var/random-seed` from one boot to the next, so two cold boots of the same image
inside the same RTC second are no longer indistinguishable. It buys that and nothing
else — the seed is credited no entropy, and a pool that reports `ENTROPY_WEAK` still
reports it.

v0.9.4 makes a file's own mode decide. Until now nothing read it: `check_vfs_access()`
was asked about the directory an operation happened in and never about the file, so
`/etc/shadow` carried `0600` inside an `/etc` that carried `0755` and any user on the
system could read it. That was a regression rather than a gap — v0.9.0 refused reads of
`shadow` by name, and v0.9.1 retired the name comparison without replacing what it did.
`chmod 600` means something now. So does the execute bit, which decides what may be run
instead of the directory a program happens to sit in, and so does the sticky bit on
`/tmp`, which is `01777` and lets a shared directory be shared without letting anyone
empty it. Three more name comparisons went with them, hidden below the syscall layer in
the VFS, where a file called `passwd` could not be created, deleted or renamed anywhere
on the system — including in your own home directory.

v0.10.0 "Stake" gives the disk a partition table and takes the 2 MB ceiling off it. The
two arrive together because they move the same bytes: sector 0 held the superblock from
v0.9.0 onward, so making room for an MBR moves every sector address behind it, and there
is no sense in moving a user's files twice. What actually capped the disk was never the
partitioning — it was the allocation table holding one entry per *sector*, a static
`uint32_t[4096]`, so 4096 sectors was the whole of it. Counting in clusters of eight
sectors covers 16 MB with the same 16 KB of kernel memory, and the bill is stated rather
than buried: a one-byte file now occupies 4 KB. The directory table is also checked
against its own invariants before anything is allowed to believe it — that an entry's id
is the slot it sits in, that a parent chain reaches the root, that a start cluster is
inside the file system — which had been an open item in `SECURITY.md` for several
releases. A disk written by v0.9.x is recognised by name and refused; the format did not
gain a converter and never has.

v1.0.0 freezes the system call interface, and the work was mostly deciding rather than
writing. Four numbers had been carrying open questions: 11 and 28 held calls that were
removed, 30 through 32 were reserved in some early release for a crypto API nobody ever
designed, and 99 held `YIELD` — far outside the run every other call sits in, for no
reason anybody recorded. All of them are settled the same way: a hole is never filled,
and new calls continue from the highest assigned number. `YIELD` moved to 67, which is an
ABI break and was the
last moment one was allowed; the byte that made it possible to move was in the idle
task's hand-assembled Ring 3 loop, which carried the number as a literal `0x63` where no
compiler could see it disagree with the header.

The fourth was `ALARM`. It kept none of the three promises its name made — no duration,
no signal, no relation to the caller — and instead armed a kernel timer that printed a
line on the console, the last call in the table that produced output from Ring 0 on a
caller's behalf. Rather than removing it the way `CAT_FILE` and `LS_DIR` went, v1.0.0
made it the call it had been claiming to be: `alarm(seconds)` delivering `SIGALRM` from a
deadline in the caller's own process control block, returning what was left on any alarm
it displaced.

What actually enforces the freeze is a test. A syscall number is written down in four
places no compiler compares — the header, the freestanding programs under `apps/bin`, the
literal strings the static analyser greps for, and the table in this file — so a comment
promising the numbers would not move would have protected none of them.
`tests/kernel/test_abi.c` asserts all 62 of them by literal value, along with the errnos,
the flags, the signal numbers and the security levels, and refuses the retired numbers
through the dispatcher.

v1.1.0 makes the disk actually confidential. Until now the file system key was a
constant compiled into the kernel, and the boot log said so in as many words: tamper
resistance, not confidentiality at rest. A machine you could carry was a machine whose
disk anyone holding the binary could read. The key now comes from a passphrase entered at
boot, through two levels — PBKDF2 turns the passphrase into a key-encryption key, which
unwraps a random data key held in the superblock. The second level is what makes changing
a passphrase cost 116 bytes instead of a pass over every file.

Two things came out of building it. The programs in `/bin` are baked into the kernel
image encrypted, and they were being written to disk exactly as the build had encrypted
them — so a passphrase would have protected the user's files while every system binary
stayed readable to anyone with the kernel. They are decrypted once and re-encrypted under
the disk key now. And the disk format moves to v3 with no fallback for a v2 one: a
"missing key slot means use the built-in key" path would be a downgrade hole written into
the format, so a v2 disk is refused by name the way v0.9.x disks have been since v0.10.0.

`SETKEY` is 68 — the first number assigned since the freeze, and the freeze working
rather than an exception to it: new calls continue from the highest assigned number, so
the next one takes 69.

v1.2.0 stops a failed disk read from looking like an empty disk. Until now the ATA driver
zeroed its buffer on every failure path and reported the outcome through a return value
nothing read — the block cache discarded it and stored the zeros as though they were
data. The check that decides whether a disk is blank read through that cache, so a drive
that failed `IDENTIFY`, a loose cable, an LBA past the end of the disk or an interrupt
that never arrived all arrived at the same place: a run of zeros, read as "this disk is
empty", and formatted. On real hardware that is data loss from a transient error. In QEMU
it never happens, which is why it survived to be found by reading rather than by breaking.

The fix needed a seam. `bcache` called `ata_read_sector()` directly, so there was no way
to put a failing device underneath it and no way to write the assertion at all. The file
system now talks to a registered block device instead, ATA registers itself as one, and
the driver's contract was corrected on the way past: `include/ata.h` had documented "0 on
success, or a negative error code on failure" while the code returned 1 for success and 0
for failure, and never a negative anything. The header and the code had disagreed about
the meaning of zero since v0.4, in the direction that turns every failure into a success.

The layer is also the thing the next few releases need. A SATA controller or a USB stick
plugs in where ATA does now, and neither of them needs the file system to know about it.

It remains a development release, intended for developers, OS enthusiasts, and
anyone curious about kernel internals — not for storing anything you care about.

**What works:**
- Boots via GRUB, initializes all subsystems, and launches a Unix-style shell
- Preemptive multitasking with ELF binary execution
- Encrypted file system with AES-256-CBC
- User authentication and security levels
- 19 user-space programs and 34 shell builtins
- 37 kernel self-test modules and CI pipeline

**What to expect:**
- This is not production-ready software
- You may encounter kernel panics, deadlocks, or unexpected behavior
- Resource limits are intentionally constrained (16 processes, 16 MB disk, 128 MB RAM,
  512 file system entries, 64-character file names)
- No networking, no GUI, no dynamic linking

---

## Features

### Kernel Core

| Component | Description |
|-----------|-------------|
| **Boot** | Multiboot-compliant entry, 16 KB kernel stack, identity-mapped first 16 MB |
| **GDT / IDT / TSS** | 9-entry GDT with Ring 0 and Ring 3 segments, 256-vector IDT with PIC remapping, one TSS for privilege transitions and a second for the double-fault task gate |
| **Syscall Interface** | 63 system calls via INT 0x80, covering process control, job control, file I/O, IPC, security, and device access |
| **Terminal (ANSI)** | Cursor positioning and relative motion, erase display and line, colour and attributes, saved cursor, scroll region, line insert and delete. Rows are counted in the 24 the screen shows; row 0 is the status bar |
| **Kernel Logging** | 512-record ring; each record carries its own level, module, monotonic timestamp and sequence number. Readable through the `dmesg` syscall and `/dev/kmsg`, controlled through `KLOG_CTL`, written to `/var/log/kern.log` at `sync`, `halt` and `reboot`. Records only — the screen transcript is not part of it |
| **Spinlocks** | Interrupt-safe kernel spinlock primitives |

### Memory Management

| Component | Description |
|-----------|-------------|
| **Physical Memory (PMM)** | Bitmap allocator with per-frame reference counting, multiboot memory map detection, 128 MB addressable, next-fit optimization |
| **Virtual Memory (Paging)** | Recursive page directory mapping, per-process address spaces, 4 KB page granularity, copy-on-write on `fork` |
| **Kernel Heap** | First-fit allocator with block splitting, bidirectional coalescing with adjacency checks, magic-number corruption detection, double-free protection |
| **User Memory** | Program break (`brk`) and anonymous `mmap`/`munmap`, both zero-filled; `include/umalloc.h` is a header-only allocator over them |

### Process Management

| Component | Description |
|-----------|-------------|
| **Scheduler** | Preemptive, priority-based, with kernel-mode preemption guard |
| **ELF Loader** | Loads PT_LOAD segments, sets up user stack with guard page, inherits file descriptors |
| **Working directory** | Per-process, held in the PCB and inherited from the creating process. Relative paths resolve against it; user space can only move it through `chdir()` |
| **IPC** | Message passing (8-slot mailbox per process), anonymous and named pipes (16 pipes, 4 KB ring buffer each) |
| **Signals** | Per-process signal handlers (32 slots), kernel timer slots (32) |
| **Job control** | Process groups, a foreground group the terminal points at, and a stopped task state. Ctrl-C interrupts the foreground job and Ctrl-Z parks it; `fg` and `bg` bring it back |
| **FPU** | Eager FPU state save/restore on every context switch (FXSAVE/FXRSTOR), per-process 512-byte state. Not lazy — there is no `CR0.TS` / `#NM` path |

### File System

| Component | Description |
|-----------|-------------|
| **VFS** | Custom FAT-based file system with flat directory table, CRUD operations, atomic file updates. Sector 0 is an MBR partition table; the file system lives in a partition and its superblock carries a magic number, a format version and the partition-relative geometry it was laid out with. The allocation table counts 4 KB clusters. An unrecognised disk is refused rather than read, a disk that cannot be read is refused rather than formatted, and the directory table is checked against its own invariants before it is believed. 512 entries of 96 bytes, each with an owner, a group, permission bits and creation and modification times |
| **CryptoFS** | Transparent AES-256-CBC encryption layer. Per-file IVs are derived as `HMAC-SHA256(file key, label ‖ counter ‖ pool bytes)`, so they stay distinct even when the entropy pool has nothing to offer; HMAC-SHA256 over the plaintext for integrity |
| **Block Cache** | 64-slot LRU sector cache, write-back. Dirty sectors are written out when 32 slots are outstanding, when any has waited 5 seconds, or on explicit `sync()`. A read the device refuses is not cached, and a sector the device will not take stays dirty so a later flush tries it again |
| **Block Device Layer** | One registered device under the cache, with the sector bounds check in one place rather than in each driver. ATA registers itself at boot; the file system never names it. A driver's errno reaches the file system unchanged, which is what lets the mount path tell a disk it cannot read from a disk with nothing on it |
| **DevFS** | `/dev/null`, `/dev/random`, `/dev/urandom` and `/dev/kmsg` device nodes; the random devices are ChaCha20 keyed from the kernel entropy pool and re-keyed periodically, and `/dev/kmsg` streams log records with a per-process cursor |

### Drivers

| Driver | Description |
|--------|-------------|
| **ATA/IDE** | PIO-mode disk I/O with IRQ-based waiting, 28-bit LBA, single-sector read/write, cache flush |
| **PS/2 Keyboard** | IRQ1 handler, US and Turkish layouts, Shift/CapsLock/AltGr, 256-byte ring buffer |
| **VGA Text** | 3 virtual terminals (F1-F3 switching), 80x100 scrollback buffer, status bar, cursor management |
| **RTC** | CMOS real-time clock, BCD/binary auto-detection, 12/24-hour conversion |

### Cryptography

| Algorithm | Description |
|-----------|-------------|
| **AES-256** | Full implementation supporting ECB, CBC, and CTR modes (based on tiny-AES-c) |
| **SHA-256** | Incremental implementation (init/update/final), verified against NIST vectors |
| **HMAC-SHA256** | RFC 2104, verified against RFC 4231 test vectors. Used for file integrity and IV derivation |
| **PBKDF2-HMAC-SHA256** | Password key derivation, verified against RFC 6070 vectors. Iteration count is clamped to a sane range on read |
| **ChaCha20** | Stream cipher backing `/dev/random` and `/dev/urandom`, keyed from the entropy pool |
| **Entropy pool** | RDRAND when available; otherwise interrupt timing jitter with per-source entropy budgets and an honest quality verdict. A 32-byte seed in `/var/random-seed` carries distinctness across boots, credited zero bits |

### User Space

| Component | Description |
|-----------|-------------|
| **Shell** | Login screen, 33 builtins (cat, ls, cd, pwd, mkdir, rm, mv, write, env, export, exec, kill, su, sleep, dmesg, hexdump, help and more), four-stage pipelines, output redirection, `&&`/`\|\|` chaining, `$VAR`/`$?`/`~` expansion, line editing with history, and Tab completion that walks its candidates |
| **Programs** | 19 standalone ELF binaries: `sh`, `edit`, `hello`, `echo`, `clear`, `touch`, `rm`, `mv`, `cp`, `free`, `whoami`, `kill`, `grep`, `head`, `wc`, `date`, `stat`, `chmod`, `chown` |
| **FHS Layout** | `/bin`, `/dev`, `/etc`, `/home`, `/root`, `/tmp`, `/var` created at boot |
| **Authentication** | Password-protected login, `/etc/shadow` database, `su` for user switching |

### Testing and CI

| Layer | Description |
|-------|-------------|
| **Kernel Self-Tests** | 37 kernel-mode modules: abi, keyslot, blockdev, string, memory, pipe, VFS, devfs, passwd, security, stress, adversarial, integration, regression, concurrency, paging, PMM, lifecycle, fork, COW, umem, fault, syscall, klog, tty, pgroup, jobctl, kbd, edit, process, signal, reap, ELF, crypto, entropy, bcache, time — plus a Ring 3 payload that exercises the privilege boundary from the unprivileged side. `make test_kernel MODULE=<name>` runs one of them alone |
| **Host Tests** | Crypto verification, ELF static analysis, ELF validation, hash validation |
| **Fuzzing** | Parser fuzz testing with 58 corpus files |
| **CI Pipeline** | GitHub Actions: host tests, fuzzing, OS build, QEMU kernel integration tests |

---

## Architecture

```
                     +--------------------------------------------------+
                     |                   User Space                     |
                     |                                                  |
                     |    init (shell)     hello     echo     clear     |
                     |         |             |        |         |       |
                     +---------|-------------|--------|---------|-------+
                               |             |        |         |
                          INT 0x80      INT 0x80  INT 0x80  INT 0x80
                               |             |        |         |
    +--------------------------|-------------|--------|---------|-------+
    |                    System Call Dispatcher (62 syscalls)           |
    +------------------------------------------------------------------+
    |                                                                   |
    |   +-------------+  +-------------+  +-------------+  +---------+ |
    |   |   Process    |  |    File     |  |   Memory    |  |  Security| |
    |   |  Management  |  |   System    |  | Management  |  |  Module  | |
    |   |             |  |             |  |             |  |          | |
    |   | - Scheduler |  | - VFS (FAT) |  | - PMM       |  | - Auth   | |
    |   | - ELF Load  |  | - CryptoFS  |  | - Paging    |  | - Levels | |
    |   | - Pipes/IPC |  | - bcache    |  | - Heap      |  | - Passwd | |
    |   | - Signals   |  | - DevFS     |  |             |  | - KDF    | |
    |   +-------------+  +-------------+  +-------------+  +---------+ |
    |                                                                   |
    |   +-----------------------------------------------------------+   |
    |   |                     Hardware Abstraction                  |   |
    |   |   ATA Disk  |  PS/2 Keyboard  |  VGA Text  |  RTC Clock  |   |
    |   +-----------------------------------------------------------+   |
    |                                                                   |
    |   +-----------------------------------------------------------+   |
    |   |                   x86 CPU Infrastructure                  |   |
    |   |   GDT (9 entries)  |  IDT (256 vectors)  |  TSS  |  PIC  |   |
    |   +-----------------------------------------------------------+   |
    +-------------------------------------------------------------------+
                               |
                         Multiboot / GRUB
                               |
                          Physical Hardware
```

### Virtual Memory Map

```
0xFFFFFFFF  +-------------------------+
            |  Recursive Page Dir     |  0xFFFFF000 - PD maps itself
0xC0000000  +-------------------------+
            |  Kernel Space           |  Identity-mapped first 16 MB
            |  (kernel code, data,    |
            |   heap, page tables)    |
0xBFFFFFFF  +-------------------------+
            |  (unmapped)             |  Top of the user range; validated as
            |                         |  writable but nothing is mapped here
0xB0000000  +-------------------------+  USER_STACK_TOP
            |  User Stack             |  32 pages, grows down
0xAFFDF000  +-------------------------+  Guard page, never mapped
            |  mmap region            |  Anonymous mappings, allocated
            |                         |  downwards from the guard page
0xA0000000  +-------------------------+  USER_MMAP_FLOOR
            |                         |
            |  (unmapped)             |  The two regions grow towards each
            |                         |  other and stop at this fixed split
            |  Program break / heap   |  Grows up from the end of the image
            |                         |
            |  User Space             |  Process-specific mappings
            |  (ELF segments)         |
0x00400000  +-------------------------+  User space lower bound
            |  Kernel Low Memory      |  First 4 MB (supervisor only)
0x00100000  +-------------------------+  Kernel load address (1 MB)
            |  Real Mode / BIOS       |
0x00000000  +-------------------------+
```

### Boot Sequence

```
GRUB (Multiboot)
  |
  v
boot.asm -----> Set up page tables (identity + higher-half)
  |              Enable paging (CR0 bit 31)
  |              Set up 16 KB kernel stack
  v
kernel_main()
  |
  +---> init_terminal()          VGA text mode, 3 virtual terminals
  +---> init_gdt()               9-entry GDT, Ring 0 + Ring 3 segments, #DF TSS
  +---> init_idt()               256 IDT entries, PIC remapping
  +---> init_security()          Seeds the entropy pool (RTC + TSC)
  +---> init_image_asset_key()   Loads the build-time key for the embedded /bin images
  +---> init_pmm()               Bitmap allocator from multiboot memory map
  +---> init_paging()            Recursive page directory, identity map 16 MB
  +---> init_kernel_heap()       First-fit heap allocator
  +---> init_timer(TIMER_HZ)     PIT at 100 Hz
  +---> init_signals()           Kernel timer slot table
  +---> ata_identify()           Brings the disk up and registers it as the root
  |                              block device. The only place ATA is named
  +---> fs_init()                Partition table, superblock, FAT and directory
  |                              table; formats a blank disk, refuses a foreign one
  |                              and refuses one it cannot read
  +---> unlock_disk_key()        Asks for the disk passphrase and unwraps the data
  |                              key from the superblock's key slot; sets one on a
  |                              disk it has just formatted. Before /etc is written
  +---> init_fpu()               FPU/SSE detection and initialization
  +---> init_multitasking()      Idle task, task array
  +---> Create FHS hierarchy     /bin, /dev, /etc, /home, /root, /tmp, /var
  +---> First boot setup         Prompts for the root and user passwords,
  |                              then writes /etc/passwd and /etc/shadow
  +---> Load ELF programs        Decrypt and write init and the /bin tools
  +---> apply_system_modes()     Put the system's own paths back to their modes.
  |                              After the programs are written, not before: they
  |                              are created 0644 and exec needs the execute bit
  +---> entropy_load_seed()      Mix /var/random-seed in, then replace it
  +---> load_and_exec_elf()      Load init into its own address space
  v
start_first_task() ------------> iret to Ring 3, init shell starts
```

---

## Quick Start

```bash
# Clone the repository
git clone https://github.com/iamfurkann/esdumanOS.git
cd esdumanOS

# Build and run in QEMU (requires toolchain, see Building section)
make run
```

The kernel boots inside your terminal (`make run` passes `-display curses`, not a
separate window). There is no default password: on a fresh
disk the first boot runs a setup prompt that asks you to choose passwords for
`root` and for the `esduman` user, and only then writes `/etc/shadow`. Subsequent
boots go straight to the login prompt.

The exception is a self-test build (`make test_kernel`), which needs to run
unattended and so creates both accounts with the password `test`.

---

## Building

### Requirements

| Tool | Minimum Version | Purpose |
|------|----------------|---------|
| `gcc` | 9.0+ | Cross-compilation (C kernel code) |
| `nasm` | 2.14+ | x86 assembly |
| `ld` | GNU ld 2.30+ | Linking |
| `make` | GNU Make 4.0+ | Build system |
| `grub-mkrescue` | 2.04+ | Bootable ISO creation |
| `xorriso` | 1.5+ | ISO 9660 filesystem (used by grub-mkrescue) |
| `mtools` | 4.0+ | FAT filesystem utilities (used by grub-mkrescue) |
| `qemu-system-i386` | 5.0+ | x86 emulation |
| `openssl` | 1.1+ | Build-time ELF encryption tooling |
| `python3` | 3.6+ | Not used by anything in this repository as of v0.9.0; kept in the install commands below because some distributions' GRUB image tooling wants it |

The host may be 64-bit; `gcc-multilib` is what lets a 64-bit compiler emit the
32-bit objects this kernel is built from, and it is what CI uses. A 32-bit host
works too but is not required by anything here.

> **Developing inside an emulated x86 VM?** The host CPU must implement `POPCNT`.
> QEMU's default `qemu64` CPU model does not, and libFuzzer's runtime uses that
> instruction — so `make fuzz` dies with `SIGILL` inside the sanitizer before any
> of this project's code runs, reported only as "deadly signal". Start the VM with
> `-cpu max` (or any model from Nehalem onward) and confirm with
> `grep -c popcnt /proc/cpuinfo`. This bites on Apple Silicon, where an x86 guest
> is always emulated; real hardware and the CI runners are unaffected. `make fuzz`
> now checks for it and says so rather than failing this way.

### Installing Dependencies

**Ubuntu / Debian:**

```bash
sudo apt-get update
sudo apt-get install -y gcc-multilib nasm make qemu-system-x86 \
    grub-common grub-pc-bin xorriso mtools libssl-dev python3
```

A minimal Debian netinst does not ship everything the build needs. Add
`make`, `git`, `xxd` (the ELF embedding step calls `xxd -i`), and `bear` if you
want a `compile_commands.json` for your editor.

**Fedora:**

```bash
sudo dnf install gcc nasm make qemu-system-x86 \
    grub2-tools-extra xorriso mtools openssl-devel python3
```

**Arch Linux:**

```bash
sudo pacman -S gcc nasm make qemu-system-x86 \
    grub xorriso mtools openssl python
```

### Build Commands

> **Required:** `ESDUMAN_ELF_KEY_HEX` must be set, and it must be **exactly 64
> hexadecimal characters** — the 32-byte AES-256 key used to encrypt the embedded
> ELF binaries. The Makefile checks both and aborts otherwise. A passphrase-style
> value will not work.
>
> ```bash
> export ESDUMAN_ELF_KEY_HEX=$(openssl rand -hex 32)
> ```
>
> The key ends up compiled into the kernel image, so treat it as a build
> parameter rather than a secret — see [Known Limitations](#security).

```bash
# Full build: compile kernel, encrypt ELF binaries, create bootable ISO
make

# Clean all build artifacts
make clean
```

Objects go into `build/<flavour>/`, mirroring the source tree — `build/prod` for the
release image, `build/test` for the self-test binary, `build/dev` for `make run-dev`.
The three differ by compiler flags that make cannot see, so they are kept apart by
directory instead: `make` and `make test_kernel` can be run in any order without a
`clean` between them, and an incremental build recompiles only what changed.

> `make ARCH=riscv64` stops with a diagnostic: the Makefile carries a RISC-V
> branch, but `arch/riscv/` is not present in this tree. Only `ARCH=x86` builds.

The build process:
1. Compiles all C and assembly source files with `-m32 -nostdlib -nodefaultlibs
   -fno-builtin` (user-space programs additionally get `-ffreestanding`)
2. Builds the 15 user-space ELF programs plus `init`
3. Encrypts each ELF with AES-256-CBC using `ESDUMAN_ELF_KEY_HEX` and embeds the
   ciphertext as a C array via `xxd -i`
4. Links the kernel binary against the custom linker script (load at 1 MB,
   higher-half at 0xC0000000)
5. Creates a GRUB-bootable ISO image via `grub-mkrescue`

---

## Running

### QEMU (Recommended)

```bash
# Build and boot in one step
make run
```

**`make run` zeroes `disk.img` first**, so every invocation is a first boot on a blank
disk. That is deliberate — a development target that starts from a known state — but it
has a consequence worth knowing before you go looking for a bug that is not there: you
will always be asked to *set* a disk passphrase, never to enter an existing one, and
nothing you write survives to the next `make run`. To see the unlock path, or to check
that a file is still there, reboot from inside the OS with `reboot` rather than leaving
QEMU. To keep a disk across sessions, boot the ISO by hand with a disk image of your own
— see [Manual QEMU Invocation](#manual-qemu-invocation).

This executes QEMU as:

```
qemu-system-i386 -cdrom esdumanOS-v1.2.0-beta.1.iso -boot d -serial file:kernel_log.txt \
    -drive format=raw,file=disk.img,if=ide,index=0,media=disk -display curses
```

Which means:
- **`-boot d`** — boot from the CD-ROM, and it is not optional once the disk has been
  formatted. The kernel writes an MBR carrying a valid `0xAA55` signature, because the
  file system uses that signature to recognise its own partition table at mount; the
  446-byte boot area is zero and nothing there was ever meant to run. The BIOS cannot
  tell those two facts apart, so without this it calls the disk bootable, jumps into
  446 bytes of zeros, and stops at `Booting from Hard Disk...`. The first boot of a
  blank disk works because a blank disk has no signature yet.
- **`-display curses`** — the OS runs inside your terminal, not a separate window.
  Quit with `Esc` then `2` to reach the QEMU monitor, or `Ctrl-A X` under `-nographic`.
- **Serial output goes to `kernel_log.txt`**, not to the terminal. That file is
  where `klog` output lands; tail it in another shell while the OS runs.
- Bootable CD-ROM from the generated ISO, plus a raw disk image on the
  primary IDE channel.
- No `-m` flag is passed, so QEMU's default for i386 applies — 128 MB, which is
  what the PMM reports at boot.
- No debug-exit device: that is added only by `make test_kernel` and `make test_smap`.

### Manual QEMU Invocation

To get a graphical window and the serial log on your terminal instead of the
defaults above:

```bash
qemu-system-i386 \
    -m 128 \
    -cdrom esdumanOS-v1.2.0-beta.1.iso \
    -boot d \
    -drive file=disk.img,format=raw,if=ide \
    -serial stdio
```

`-boot d` is required for every boot after the first: a formatted disk carries an MBR
signature with no boot code behind it, and the BIOS will try it and stop. See the note
above.

Add `-device isa-debug-exit,iobase=0xf4,iosize=0x04` only if you want the kernel
to be able to terminate QEMU with an exit code, as the self-test targets do.

The ISO filename carries the version, and the Makefile derives it from the
`OS_VERSION_*` macros in `include/kernel.h` — so it changes on a version bump and
there is no second place to update. Run `make -pn | grep '^ISO ='` if you are not
sure what the current build produces.

### Keyboard Shortcuts (Inside the OS)

| Key | Action |
|-----|--------|
| F1 / F2 / F3 | Switch between virtual terminals |
| Shift-PgUp / Shift-PgDn | Scroll terminal history |
| Arrows, Home, End, Delete, PgUp, PgDn | Sent to the program as escape sequences |
| AltGr | Access Turkish keyboard layout characters |
| Ctrl-D | End input for a program reading the keyboard (`cat`, `grep`, `head`, `wc`) |
| Ctrl-C | Interrupt the foreground job — every process in it, not just one |
| Ctrl-Z | Stop the foreground job and get the prompt back; `fg` or `bg` resumes it |

Ctrl with a letter produces that letter's control code, which is how Ctrl-D becomes the
end-of-file byte. Ctrl with anything else is passed through unchanged.

The navigation keys send what a terminal sends: `ESC [ A` through `ESC [ D` for the
arrows, `ESC [ H` and `ESC [ F` for Home and End, and `ESC [ 2 ~`, `ESC [ 3 ~`,
`ESC [ 5 ~`, `ESC [ 6 ~` for Insert, Delete and the Page keys. A sequence is placed in the
input ring whole or not at all, because a reader handed half of one would wait for a byte
that is not coming.

**Scrollback moved off the arrow keys in v0.8.2.** It had them because nothing else
wanted them; the arrows are now the only way a program can be told where to move, and a
key the driver consumes is a key no program will ever see. Shift with the Page keys is
where every terminal emulator puts scrollback.

Ctrl-C and Ctrl-Z are consumed by the driver rather than delivered as bytes: they name
the foreground process group and send it a signal. A program that wants the interrupt
itself asks for it by catching `SIG_INT`; leaving the byte in the input ring as well
would hand every reader a stray control character after every keypress.

**Ctrl-Z does not reach the guest under `-display curses`.** The key gets as far as the
terminal QEMU is running in — `stty susp undef; cat -v` echoes `^Z` there — but the
curses front end does not turn it into a guest keypress, and `stty susp undef` before
launching does not change that. Ctrl-C and Ctrl-D are delivered normally.

The symptom is unambiguous, because the `^Z` echo happens in the driver before
anything else: no `^Z` on screen means nothing arrived. Two ways to reach the same
place, both verified:

```
Esc, then 2          (QEMU monitor)
sendkey ctrl-z
Esc, then 1          (back to the guest)
```

or send the signal from inside the OS, which needs no monitor at all:

```
sleep 30 &
kill <pid> 20
jobs
fg
```

A graphical display backend delivers the key without any of this; curses is the one
that does not.

### Shell Commands

Once logged in, the following builtins are available:

```
cat [-nbEsTA] <file>  Print file contents
ls                    List the working directory (takes no argument)
cd [dir]              Change directory; supports ., .., ~ and -
pwd                   Print working directory
mkdir <name>          Create directory
rm <file>             Remove file
mv <old> <new>        Rename a file within one directory
write <file> <text>   Create a file with the given contents
cat_raw <file>        Hex dump a file's stored bytes, bypassing decryption
env                   List environment variables
export KEY VALUE      Set environment variable (two words, not KEY=VALUE)
sleep <seconds>       Pause for a number of seconds
exec <program>        Execute an ELF binary
jobs                  List the jobs this shell started and still tracks
fg [%n]               Bring a job to the foreground and wait for it. With no
                      argument, the most recent one.
bg [%n]               Continue a stopped job without giving it the terminal
wait                  Block until every background job has finished
kill <pid> <signal>   Send a signal to a process (decimal). 9 and 15 terminate a
                      target that has not registered a handler for them; 20 stops
                      it and 18 continues it. A negative pid names a process group
                      and signals every member.
su                    Switch to root (prompts for the root password)
dmesg                 Display the kernel log
meminfo               Display memory usage (root only)
hexdump <addr>        Hex dump memory (root only)
stack                 Dump the current task's stack (root only)
date [-u]             Show the date and time; -u for UTC
date -s "..."         Set the clock, as "YYYY-MM-DD HH:MM:SS" (root only)
layout tr|us          Set the keyboard layout
lockdown              Enter the lockdown security level
help                  Show available commands
reboot                Reboot the system
halt                  Halt the CPU
exit                  Exit the shell
```

`echo` and `clear` are not builtins — they are ELF programs in `/bin`, reached
through the same path as any other program. `echo` does not implement `-n`.

**Tab completes, and Tab again walks.** The first press finishes a single match, or
narrows to as much as the candidates agree on, or lists what there is. Pressing Tab
again replaces the word with the next candidate and keeps going, wrapping at the end.
Any other key ends the walk — the list belonged to one word in one state of the line.

### Permissions

Every file and directory carries a mode, an owner and a group, and as of v0.9.1 they
are what decides access.

```
chmod 644 notes.txt        # owner reads and writes, everyone else reads
chmod 700 private          # owner only
chown 1000 notes.txt       # owner and group both become 1000 (root only)
chown 1000:1000 notes.txt  # the same, said explicitly
ls -l                      # mode, owner:group, size and modification time
```

Modes are octal only — no `u+x`. `chmod` is the owner's and root's; `chown` is root's
alone, because giving a file away is how a user escapes a quota on a system that has
one, and this restriction is easier to keep than to add back.

The rule is the Unix one, and it is asked twice. Reaching a file needs search permission
on every directory above it and read or write on the directory the operation happens in;
then the file's own bits are asked the same question. The matching class decides and only
that class: an owner with no permission is refused rather than falling through to the
group bits, which is what makes `chmod 077` mean what it says.

The execute bit decides what may be run. Before v0.9.4 that was decided by location —
anything in `/bin` for anybody, anything else for root alone — so a program's
executability was a property of where it sat. A program you wrote and `chmod 755`'d runs.

`/tmp` is `01777`. The sticky bit is what makes a world-writable directory survivable:
write permission on a directory is permission to remove things from it, so without it
anyone could delete anyone else's temporary file. With it, removal and renaming are
restricted to the owner of the entry, the owner of the directory, and root. `ls -l` shows
it as `t` in the last position.

The system's own paths are set at boot and put back if they drift: `/etc/shadow` and
`/var/random-seed` are `0600`, `/tmp` is `01777`, `/root` is `0700`, everything in `/bin`
is `0755`, and the rest of the top level is `0755`. Files are created `0644` and
directories `0755`.

### The Editor

`edit <file>` opens the system's text editor. It is modal, in the shape `vi` has: keys
are commands until `i` puts it in insert mode, and Escape brings it back. A file that
does not exist yet is not an error — it is a new file, and `:w` writes it.

| | |
|---|---|
| **Moving** | `h` `j` `k` `l` or the arrows, `0` and `$` or Home and End, `gg` and `G` for the first and last line, Page Up and Page Down |
| **Editing** | `i` insert here, `a` after the cursor, `A` at the end of the line, `o` and `O` open a line below or above, `x` delete a character, `dd` delete a line |
| **Undoing** | `u` takes back the last thing you did |
| **Searching** | `/<pattern>` finds it, `n` and `N` repeat forwards and backwards, `/` alone repeats the last pattern |
| **Commands** | `:w` write, `:w <name>` write as, `:q` quit, `:q!` quit without writing, `:wq` both, `:<number>` jump to a line |
| **Other** | Ctrl-L redraws, Ctrl-Z stops it and `fg` brings it back with the screen intact |

Ctrl-C is declined: throwing away an unsaved buffer should take more than one key, and
`:q!` is there for anyone who means it. A file is at most 64 KB, which is the most this
file system will write back.

`u` gives back a whole change rather than a byte: a sentence typed in insert mode is one
thing you did, so one press takes all of it. Each command in normal mode is its own step.
There is no redo — `u` keeps going backwards rather than toggling — and the log keeps 256
changes and 8 KB of deleted text, past which the oldest changes are given up and the
status line says so.

Search is plain text, not a pattern language, and it wraps in both directions: the status
line says when it went round the end, which is the only way to tell one match from the
same match found again.

A newline separates lines rather than ending them, so a file that ends in one shows an
empty line after it — where `vi` would show only the text. That is the honest reading for
an editor whose buffer is the file's bytes: pressing Enter at the end of the last line
produces exactly that newline, and a screen that did not show the new line would be
hiding the one thing the key just did.

It is the first program here to draw a full screen, and therefore the first consumer of
the escape sequences v0.8.0 taught the terminal and the keys v0.8.2 taught the keyboard.

**Operators:** Pipes (`cmd1 | cmd2 | cmd3`, up to four stages), output redirection
(`cmd > file`), chaining (`cmd1 && cmd2`, `cmd1 || cmd2`).

Four stages is a process budget rather than a preference: an external stage costs two
tasks, because the forked child runs the program through `exec()`, which creates a task of
its own. A fifth is refused with a message, as are a bare `|` and a backgrounded pipeline.

**Editing the line.** The left and right arrows move within the line and typing inserts
where the cursor is; Home and End jump to the ends, Delete removes forward and Backspace
back. The up and down arrows walk the last eight commands, and the line being typed is
kept while you do, so walking back returns it rather than an empty prompt. The password
prompt deliberately has none of this — there is nothing to navigate in a field displayed
as asterisks.

A command word containing a `/` is a path and is never matched against the builtin table,
so `rm` is the builtin and `/bin/rm` is the program of that name.

**Reading standard input.** `cat`, `grep`, `head` and `wc` read standard input when no
file is named, which is what lets them sit at the far end of a pipe:

```
echo hello | grep hello
cat /etc/passwd | wc
ls /bin | grep wc
dmesg | head
```

Run bare, they read the keyboard instead, and **Ctrl-D** ends that input. A program
writing into a pipe whose reader has finished is sent `SIGPIPE` and terminates, so
`something | head` stops the producer once `head` has its ten lines rather than letting it
run to the end of its input. The shell declines `SIGPIPE` for itself — losing it would end
the session — and restores the default in every process it forks.

Redirection truncates: the target is created if it does not exist and emptied if it does.
It can be combined with a pipe, and belongs to the stage it appears in — in `a | b > f`,
`b`'s output goes to the file.

**Writing a file is all-or-nothing.** Bytes written through a descriptor are held
until the last descriptor closes and then committed as the file's entire new
contents, so there is no appending (`>>`), no writing into the middle of a file,
and no seeking during a write. That is not a shortcut: a stored file is a single
AES-CBC blob authenticated over its whole plaintext, so it can be replaced but
never extended. A single file is capped at 64 KB on this path.

**Variables:** `$VAR` expansion, `$?` last exit code, `~` home directory. Expansion runs
over the whole line before it is split into stages, so a variable in any stage is
expanded, and each `~` gets storage of its own.

**Startup files.** The shell reads three files under `/etc` before its first prompt, and
carries on without any of them. `/etc/hostname` supplies the name in the prompt,
`/etc/motd` is printed, and `/etc/profile` is applied — only `export KEY VALUE` lines,
because it is a settings file rather than a script. `/etc/timezone` is read by the kernel
rather than the shell, and holds a signed hour offset rather than a zone name.

---

## Testing

esdumanOS includes a multi-layered test infrastructure:

```bash
# Run host-side unit tests (crypto, ELF analysis, hash)
make test

# Run parser fuzzing with 58 corpus files
make fuzz

# Boot kernel in self-test mode: 37 kernel-mode modules, then a Ring 3 payload
make test_kernel

# Run one module instead of all of them, for iteration
make test_kernel MODULE=fork

# Run only the Ring 3 payload
make test_kernel MODULE=ring3

# Same suite on a CPU that exposes RDRAND, to cover the strong-entropy path
make test_kernel QEMU_TEST_CPU="-cpu qemu32,+rdrand"

# Same suite with SMEP/SMAP enforced. The kernel-mode modules are skipped there
# (they stand in for user space from Ring 0, which SMAP forbids); the Ring 3
# payload covers the boundary from the correct side.
make test_smap
```

`MODULE=` names one of the modules in the table at the top of
`tests/kernel/selftest.c`, or `ring3` for the user-mode payload alone. It exists
because a full run boots the OS and executes everything, which costs minutes on an
emulated host whether or not the change under test touches any of it. An unknown
name lists what is available and fails the run rather than executing nothing and
reporting a pass. **CI always runs the full suite**, and so must a release: a
filtered run proves one module, not the tree.

### Kernel Test Modules

| Module | Coverage |
|--------|----------|
| `test_abi.c` | The frozen v1.0.0 interface, asserted by literal value: all 62 syscall numbers, the 34 error codes, the `exec`/`wait`/`lseek`/`klog_ctl` constants, the signal numbers and the security levels — plus that each retired number (11, 28, 30, 31, 32, 99) is still answered with `E_NOSYS` and that 68 is still free |
| `test_blockdev.c` | The seam between the file system and its storage, against a device made up for the occasion: reads and writes reach the registered device at the sector asked for, a sector past its capacity is refused without the driver being called, a driver's errno arrives unchanged rather than flattened, a read-only device answers `E_ROFS` instead of calling a null handler, and with nothing registered both entry points answer `E_NODEV` |
| `test_keyslot.c` | The passphrase key slot, against the slot alone — no disk, no prompt: round trip, a wrong passphrase refused with the caller's buffer left untouched, every field of the slot edited in turn to confirm the tag covers the salt and IV as well as the ciphertext, iteration counts above and below what the build accepts, and the same data key wrapped under a second passphrase to prove a passphrase change preserves it |
| `test_vfs.c` | File create/delete, directory nesting (15 levels), path resolution, the on-disk format (that an entry is exactly 96 bytes and the master boot record exactly one sector, that the superblock's geometry leaves room for the regions it describes and stays relative to the partition, that clusters 0 and 1 are reserved so a start cluster of 0 can mean "no data", that a file spanning two clusters reads back byte-for-byte, that an entry id past 255 survives the trip to a sector and back, and that a name one byte too long is refused rather than shortened), the mount-time table validator against four kinds of corruption, and that the system's own paths carry the permissions they must after a boot |
| `test_memory.c` | Heap allocation, deallocation, read/write verification |
| `test_pipe.c` | Pipe creation, ring buffer, EOF detection, syscall integration |
| `test_security.c` | Authentication: wrong password, invalid UID, correct password. The permission rule the VFS decides with: the three classes, that the matching one is the only one consulted so an owner with no permission is not passed to the group, that every bit asked for has to be granted, and that root is subject to none of it |
| `test_passwd.c` | `/etc/passwd` protection: delete/overwrite/rename rejection |
| `test_adversarial.c` | Pointer validation: NULL, kernel space, upper bounds |
| `test_stress.c` | FD exhaustion (16 limit), long filename handling |
| `test_regression.c` | 5 previously-fixed bugs: kfree(NULL), PID confusion, ATA limits |
| `test_integration.c` | Cross-component: VFS lifecycle, ELF load-to-process |
| `test_concurrency.c` | Hardware atomic operations (`__sync_lock_test_and_set`) |
| `test_devfs.c` | Device filesystem: `/dev` nodes, invalid device rejection, DRBG re-keying |
| `test_string.c` | libft string function correctness |
| `test_paging.c` | Map/unmap, collision rejection, CR0.WP enforcement on read-only user pages |
| `test_pmm.c` | Frame allocation, free-memory accounting |
| `test_lifecycle.c` | Address space clone and teardown; asserts every frame is reclaimed |
| `test_fault.c` | Double-fault infrastructure: task gate, TSS descriptor, dedicated stack |
| `test_syscall.c` | Dispatcher rejection of bad numbers, FDs, and sizes, and that the diagnostics render into the caller's buffer: that they report what they wrote, that a buffer too small is filled rather than overrun, and that one the caller does not own is refused |
| `test_process.c` | Scheduler, live-frame detection, rwlocks, syscall restart, idle task |
| `test_signal.c` | Handler registration and pending-signal bookkeeping |
| `test_jobctl.c` | Stopping and continuing a task: the state it remembers, who is told, and what happens to the terminal |
| `test_kbd.c` | Scancode translation: which keys become escape sequences, which are consumed, and that a sequence is written whole or not at all |
| `test_edit.c` | The line arithmetic `/bin/edit` is built on: where a line starts and ends, what a vertical move keeps, what deleting one takes with it, what a search finds and wraps past, and what undo gives back — including which changes it drops when its log runs out |
| `test_elf.c` | Loader validation: bad sizes, overflowing offsets, kernel load addresses |
| `test_crypto.c` | SHA-256 / HMAC / PBKDF2 against published vectors; CryptoFS round trip |
| `test_entropy.c` | Extraction uniqueness, per-source entropy budgets, IV and salt distinctness |
| `test_bcache.c` | Cache hits, and the write-back policy: volume bound, time bound, `sync()` |
| `test_time.c` | Calendar carry both ways, leap years including the century rule, the `TIME` syscall, and the conversions to and from the Unix epoch — checked against each other across sixty years, with the leap rules asserted as rules rather than as dates worked out by hand. Setting the clock too: that a time set reads back as itself, that a leap day is accepted in a leap year and refused otherwise, and that a refused date leaves the clock where it was |

The Ring 3 payload (`tests/user/ktest_user.c`) runs after these and reports its
results back through a dedicated syscall. It is the only part of the suite that
actually crosses the privilege boundary, covering user-pointer validation, pipes,
`readdir`, process teardown, and lockdown behaviour from an unprivileged process.

---

## Project Layout

```
esdumanOS/
|
|-- arch/
|   +-- x86/
|       |-- boot/
|       |   +-- boot.asm             Multiboot entry, page table setup, stack init
|       |-- cpu/
|       |   |-- gdt.c                Global Descriptor Table (9 entries)
|       |   |-- idt.c                Interrupt Descriptor Table, PIC remapping
|       |   |-- isr.c                Interrupt dispatcher, exception handlers
|       |   |-- timer.c              PIT configuration (TIMER_HZ = 100 Hz)
|       |   +-- tss.c                Task State Segment, double-fault TSS
|       +-- linker.ld                Kernel linker script (load at 1 MB)
|
|-- kernel/
|   |-- core/
|   |   |-- kernel.c                 Entry point, subsystem initialization
|   |   |-- klog.c                   Ring buffer kernel logger
|   |   +-- uaccess.c                Validated copies across the user boundary
|   |-- proc/
|   |   |-- process.c                Scheduler, IPC, mutexes, context switch
|   |   |-- elf.c                    ELF binary loader
|   |   |-- elf_validate.c           Header and segment validation (also fuzzed)
|   |   |-- pipe.c                   Anonymous and named pipes
|   |   +-- signal.c                 Timer-based kernel timers
|   |-- syscall/
|   |   |-- syscall.c                Dispatcher, 63 system calls
|   |   +-- sys_*.c                  Handlers by area: fs, ipc, process, sec, utils
|   +-- security/
|       |-- security.c               Security levels, master key lifetime
|       |-- entropy.c                Entropy pool, RDRAND and interrupt jitter
|       +-- passwd.c                 User authentication, shadow database
|
|-- mm/
|   |-- pmm.c                        Physical memory manager (bitmap)
|   |-- paging.c                     Virtual memory, page directory cloning
|   |-- paging_s.asm                 CR3/CR0 helpers
|   +-- kheap.c                      Kernel heap allocator
|
|-- fs/
|   |-- vfs.c                        Virtual file system, FAT, directory table
|   |-- crypto_fs.c                  AES-256-CBC transparent encryption
|   |-- bcache.c                     Block cache (64-slot LRU, write-back)
|   +-- devfs.c                      Device filesystem (/dev/null, /dev/random, /dev/urandom)
|
|-- drivers/
|   |-- ata.c                        ATA/IDE PIO disk driver
|   |-- keyboard.c                   PS/2 keyboard (US + Turkish)
|   |-- tty.c                        VGA text mode, 3 virtual terminals
|   +-- rtc.c                        Real-time clock
|
|-- crypto/
|   |-- aes.c                        AES-256 (ECB / CBC / CTR)
|   |-- sha256.c                     SHA-256, incremental and one-shot
|   |-- hmac.c                       HMAC-SHA256
|   |-- pbkdf2.c                     PBKDF2-HMAC-SHA256
|   +-- chacha20.c                   ChaCha20 stream cipher
|
|-- lib/                             Freestanding standard library (48 files)
|   |-- stdio.c                      kvsnprintf, printk
|   +-- ft_*.c                       String, memory, character, list utilities
|
|-- apps/
|   |-- init.c                       User-space shell and login
|   +-- bin/
|       |-- sh.c                     Standalone shell
|       |-- hello.c                  Hello World ELF program
|       |-- echo.c                   Echo command
|       |-- clear.c                  Clear screen
|       |-- touch.c                  Create empty file
|       |-- rm.c                     Remove file
|       |-- mv.c                     Move/rename file
|       |-- cp.c                     Copy file
|       |-- free.c                   Display memory info
|       |-- whoami.c                 Print current user
|       |-- kill.c                   Send signal to process
|       |-- grep.c                   Search text patterns (file or stdin)
|       |-- head.c                   Display first lines of file or stdin
|       |-- wc.c                     Count lines, words and bytes
|       |-- date.c                   Display date and time, and set the clock
|       |-- edit.c                   Modal text editor, vi-shaped
|       |-- chmod.c                  Change permission bits
|       |-- chown.c                  Change owner and group
|       +-- stat.c                   Show a file's size, type, owner, mode and times
|
|-- include/                         44 header files
|   |-- kernel.h                     Master header, and where the version lives
|   |-- types.h                      Integer type definitions
|   |-- syscall.h                    62 syscall number definitions
|   |-- process.h                    Process control block, scheduler API
|   |-- fs.h                         VFS structures, file operations
|   |-- stat.h                       esd_stat_t and the lseek origins
|   |-- paging.h                     Virtual memory constants
|   |-- entropy.h                    Entropy pool API and quality contract
|   +-- security.h                   Security level enumeration
|
|-- tests/
|   |-- kernel/                      37 kernel-mode test modules + framework
|   |-- user/                        Ring 3 test payload
|   +-- host/                        Host-side tests, fuzzing (58 corpus files)
|
|-- tools/                           Build-time utilities
|   +-- encrypt_tool.c               ELF encryption tool (links OpenSSL libcrypto)
|
|-- grub/
|   +-- grub.cfg                     GRUB bootloader configuration
|
+-- Makefile                          Build system. Only ARCH=x86 is present; the
                                      ARCH=riscv64 branch is scaffolding and
                                      stops with a diagnostic.
```

---

## System Call Reference

The kernel exposes 63 system calls through `INT 0x80`. The syscall number is passed in `EAX`.

**These numbers are frozen as of v1.0.0.** None of them will change value or meaning. The
numbers 11, 28, 30, 31, 32 and 99 are retired and will never be reused — they held calls
that were removed, a reservation for a crypto API that was never designed, and `YIELD`
before it moved to 67 — and new calls continue from the highest assigned number, which is
why `SETKEY` took 68 in v1.1.0 and the next call takes 69. Everything from 200 up is
reserved for calls that exist only in test builds. `tests/kernel/test_abi.c` asserts all
of it by literal value, because a number written in a header, in freestanding programs,
in a static analyser's patterns and in this table is a number no compiler is comparing.

### Process Management

| Number | Name | Description |
|--------|------|-------------|
| 1 | `EXIT` | Terminate the current process |
| 5 | `EXEC` | Load and execute an ELF binary. Blocks until it exits and returns its status, or with `EXEC_NOWAIT` (1) in ecx returns its pid straight away |
| 7 | `SET_PRIORITY` | Set process scheduling priority |
| 51 | `GETPID` | Get the process ID of the caller |
| 52 | `SLEEP` | Block the caller for a number of **milliseconds** |
| 53 | `FORK` | Duplicate the caller; returns 0 in the child and its pid in the parent |
| 54 | `WAIT` | Collect what a child has to report; blocks unless `WNOHANG` (1). With `WUNTRACED` (2) a child that stopped is reported too, as `WSTATUS_STOPPED` (0x100) OR'd with the signal |
| 55 | `TIME` | Fill an `esd_time_t` with the current wall-clock time |
| 66 | `SETTIME` | Set the wall clock from an `esd_time_t`; the offset it carries is taken back out and UTC is stored. Root only |
| 56 | `BRK` | Move the program break; returns the resulting break, so a refusal is the break unmoved |
| 57 | `MMAP` | Map anonymous, private, zeroed pages; returns the address or `0xFFFFFFFF` |
| 58 | `MUNMAP` | Release pages obtained from `MMAP`; refuses any range outside that region |
| 59 | `KLOG_CTL` | Inspect and control the kernel log: clear it, move the severity threshold, read the held and dropped counts |
| 60 | `SETPGID` | Place a process in a process group; the caller may move itself or a child |
| 61 | `TCSETPGRP` | Hand the terminal to a process group, which is what Ctrl-C reaches |
| 62 | `GETPGID` | Read a process's group |
| 63 | `POLL` | Whether a read on a descriptor would block: 1 no, 0 yes. End of file counts as "no" |
| 67 | `YIELD` | Voluntarily yield the CPU. Was 99 until v1.0.0, which moved it as the last ABI break the freeze permitted; 99 is retired |

`TIME` fills an `esd_time_t` (`include/esdtime.h`), shared verbatim with user space the
same way `esd_stat_t` is. The fields are broken down — year, month, day, hour, minute,
second — rather than a count of seconds since an epoch: the RTC reports them that way and
nothing in this system agrees on an epoch to count from. A non-zero second argument asks
for UTC instead of local time, which is what `date -u` passes; the shift between them is
done in the kernel so the calendar carry exists in one place.

`tz_offset_hours` says how far ahead of UTC the other fields are. It comes from
`/etc/timezone`, which holds a signed hour count rather than a zone name — there is no
timezone database here, and `Europe/Istanbul` would be a name nothing could look up. The
RTC itself is assumed to hold UTC, which is what QEMU presents by default; a machine whose
CMOS holds local time wants an offset of 0. There is no daylight saving.

Setting the clock is `SETTIME` (66), added in v0.9.2 and root only. This paragraph said
there was no way to set it for five releases after there was — the row for the call sits
in the table two screens above this line, which is the whole difficulty with documentation
that is organised by topic and audited by section.

`EXEC` returns a negative errno when the program could not be started, and otherwise
the child's exit status once it has finished — statuses are masked to 0-255, so the
two cannot be confused. It blocks the caller on `WAIT_CHILD` for the child's lifetime,
which is how it returns a status at all, and it predates `WAIT` rather than being
replaced by it.

`FORK` shares the caller's user pages copy-on-write rather than duplicating them: the
child maps the parent's own frames, both sides give up write access to anything that was
writable, and the first write from either splits that page. A page that was already
read-only is shared as it stands, so writing to a program's text is still an access
violation rather than a copy. Descriptors, working directory, uid, signal handlers,
priority and FPU state are carried over. It returns `E_AGAIN` when `MAX_TASKS` live tasks
already exist and `E_NOMEM` when the address space could not be built.

`WAIT` returns a child's status, or `E_CHILD` when the caller has none left to wait for —
an error rather than a block, so a parent that called it one time too many does not sleep
until the machine is rebooted. A child that finishes while its parent is busy leaves its
status in a fixed table until it is collected, which is what makes both orders work: the
parent arriving first and waiting, and the child finishing first.

`SLEEP` takes milliseconds rather than seconds because `TIMER_HZ` is 100, giving a
10 ms resolution that a seconds-only call could not reach; the shell's `sleep`
builtin still takes seconds and multiplies. A sleeping task holds `WAIT_TIMER`
with an absolute deadline in its PCB, and `schedule()` wakes it once that tick
passes — not `wakeup_tasks(WAIT_TIMER)`, which would wake every sleeper at once
regardless of what each one asked for.

### I/O and File Descriptors

| Number | Name | Description |
|--------|------|-------------|
| 3 | `READ` | Read from a file descriptor (stdin, pipe, file) |
| 4 | `WRITE` | Write to a file descriptor (stdout, pipe, file) |
| 36 | `PIPE` | Create an anonymous pipe (returns read/write FDs) |
| 37 | `DUP2` | Duplicate a file descriptor |
| 38 | `CLOSE` | Close a file descriptor |
| 40 | `OPEN` | Open a file and return a file descriptor |
| 49 | `FSTAT` | Report an open descriptor's metadata |
| 50 | `LSEEK` | Reposition an open file's read offset |

`READ` returns 0 for end-of-file from all three sources: a pipe whose last writer has
closed, a file read past its end, and the console once Ctrl-D has been typed. A `WRITE`
to a pipe with no readers left returns `-EPIPE` **and** raises `SIGPIPE` against the
caller, whose default action is to terminate it — a writer that means to survive has to
register `SIG_IGN` for it first and then check the return value.

### File System

| Number | Name | Description |
|--------|------|-------------|
| 8 | `CREATE_FILE` | Create a new file |
| 9 | `LIST_FILES` | List files in current directory |
| ~~11~~ | — | Was `CAT_FILE`; removed in v0.9.2. It printed a file from the kernel and had no caller. The number is left free until the ABI is frozen |
| 22 | `RM_FILE` | Delete a file |
| 23 | `MV_FILE` | Rename a file |
| 26 | `MKDIR` | Create a directory |
| ~~28~~ | — | Was `LS_DIR`; removed in v0.10.0. It printed a listing from the kernel and had no caller left after v0.9.2 moved `ls` onto `READDIR`. The number is left free until the ABI is frozen |
| 29 | `GET_DIR_ID` | Resolve directory path to ID |
| 34 | `READ_RAW` | Read an open file's stored bytes, without decrypting them. `read()` against the stored form: same descriptor, same offset, same shape |
| 44 | `READDIR` | Read directory entries into user buffer |
| 45 | `SYNC` | Write dirty block-cache sectors out, and the kernel log and the entropy seed to `/var` |
| 46 | `CHDIR` | Change the calling process's working directory |
| 47 | `GETCWD` | Write the working directory into a user buffer |
| 48 | `STAT` | Report a path's metadata into a user `esd_stat_t` |
| 64 | `CHMOD` | Set a path's permission bits. The owner and root, and nobody else |
| 65 | `CHOWN` | Set a path's owner and group. Root only |

Relative paths in every one of these resolve against the calling process's working
directory, which lives in the PCB. No syscall takes a base directory as an argument.

`STAT` and `FSTAT` fill an `esd_stat_t` (`include/stat.h`), shared verbatim with
user space. Two points are worth stating plainly:

- **`st_size` is not the size on disk.** Under `SEC_LEVEL_CRYPTO_ENFORCED` — the
  default — files are stored as an IV, a header and the plaintext padded to an AES
  block, so the directory table's length is always larger than what `read()`
  returns. `st_size` comes from `fs_size()`, which reads the real length out of the
  file's header; `st_disk_size` reports the stored length alongside it. `LSEEK`
  with `SEEK_END` uses the same source, so seeking to the end lands on the end of
  the data rather than somewhere inside the padding.
- **`st_size` is not authenticated.** For an encrypted file that length is read
  from the file's own header, and the HMAC that would vouch for it covers the
  plaintext and is only checked by `read()` over the whole file. Someone able to
  write to the disk can make `stat` report a wrong size; the read that follows
  fails, but the size on its own proves nothing.

`st_mtime` and `st_ctime` arrived with the v0.9.0 format, as seconds since the Unix
epoch **in UTC**. `st_mtime` moves when the contents change and `st_ctime` when the entry
does, so a rename touches the second and not the first. Zero means the field was never
set, which nothing on a disk this kernel will mount can be — it refuses older formats —
and it is reported as zero rather than as a date that would read like an answer.

The syscall reports UTC and `stat` displays local time, with the offset printed
alongside. Storage has to be UTC or the count is not an epoch: two files stamped either
side of a timezone change would otherwise compare by what the clock said rather than by
when it happened. Display does not — nobody reads their own files in UTC — so `/bin/stat`
asks the clock for the offset the system is set to and puts it back on, printing
`2026-08-24 12:00:00 +03` rather than the same instant as `09:00:00 UTC`.

`st_mode` is the permission bits, and as of v0.9.1 they are what decides access. `chmod`
sets them, `chown` sets the owner and group, and `ls -l` shows both.

### IPC and Signals

| Number | Name | Description |
|--------|------|-------------|
| 2 | `IPC_SEND` | Send a message to another process |
| 6 | `IPC_RECEIVE` | Receive a message from mailbox |
| 18 | `ALARM` | Arms, re-arms or cancels the caller's alarm; `ebx` is a count of seconds and 0 cancels. Returns the seconds left on the previous alarm, or 0 when there was none. `SIGALRM` (14) arrives when the deadline passes and terminates the process by default. An interval past ~249 days is refused with `E_INVAL`. Real as of v1.0.0 — before that it took no argument, sent no signal, and printed a line from inside the kernel |
| 24 | `SIGNAL_REG` | Register a signal handler, or `SIG_DFL` (0) / `SIG_IGN` (1) |
| 25 | `KILL` | Send a signal to a process, or to every member of a group when the pid is negative |
| 27 | `SIGRETURN` | Return from signal handler |

Five signals terminate a process that has not handled them: `SIGKILL` (9), `SIGTERM`
(15), `SIGPIPE` (13), `SIGINT` (2) and `SIGALRM` (14). The exit status is 128 plus the
signal number, so a `wait()` reporting 141 means the child was killed writing to a broken
pipe.

Two stop it instead: `SIGTSTP` (20), which is what Ctrl-Z sends, and `SIGTTIN` (21),
which a background job gets for trying to read the terminal. A stopped process keeps its
memory, its descriptors and the instruction it was on, and `SIGCONT` (18) puts it back.

`SIGCONT` is acted on where it is sent rather than where it is delivered, because a
stopped process never reaches a delivery point of its own. A process that registered a
handler is told as well, once it is running again: the default action needs no delivery,
but a full-screen program has to redraw what the shell wrote over its display while it
was away, and there is no other moment it could learn to.

Every other signal is recorded and dropped when no handler is registered.

A disposition is one of three things: an address to jump to, 0 for the default action, or
1 for `SIG_IGN`, which discards the signal. Dispositions are inherited across `fork()`
and reset to the default by `exec()` — which is why the shell can ignore `SIGPIPE` for
itself and still has to restore the default in each stage it forks.

### Security and Cryptography

| Number | Name | Description |
|--------|------|-------------|
| 13 | `LOCKDOWN` | Enter security lockdown mode |
| 33 | `SET_SEC_LEVEL` | Set kernel security level |
| 35 | `SETUID` | Change effective user ID (requires password) |
| 41 | `AUTH` | Authenticate user against shadow database |
| 43 | `GETUID` | Get user ID of current process |
| 68 | `SETKEY` | Change the disk passphrase; `ebx` is the old one, `ecx` the new one. Root only. Re-wraps the data key under a fresh salt rather than re-encrypting the disk, so a wrong old passphrase returns `E_ACCES` and changes nothing |

*Note: 30-32 are retired, not reserved. They were set aside for a crypto API that was
never designed, and v1.0.0 retired them with the other holes — this line said "reserved
for future crypto API" for one release after that stopped being true.*

### System and Debug

| Number | Name | Description |
|--------|------|-------------|
| 10 | `CLEAR_SCREEN` | Clear the terminal |
| 12 | `SET_LAYOUT` | Switch keyboard layout |
| 14 | `STACK_DUMP` | Dump current stack (root only) |
| 15 | `MEMINFO` | Display memory usage statistics (root only) |
| 16 | `TEST_MALLOC` | Debug: test heap allocation |
| 17 | `HEXDUMP` | Hex dump memory region (root only) |
| 19 | `PANIC` | Trigger kernel panic (root only) |
| 20 | `REBOOT` | Reboot the system (root only) |
| 21 | `HALT` | Halt the CPU (root only) |
| 39 | `DMESG` | Copy a slice of the kernel log into a user buffer (root only) |
| 42 | `GET_ARGS` | Retrieve process command-line arguments |

---

## Security Model

esdumanOS implements a multi-layered security model:

### Security Levels

The kernel operates under one of four escalating security levels. Once raised, the level cannot be lowered.

```
Level 0: NORMAL             Standard operation. Encryption is optional.
                            |
Level 1: CRYPTO_ENFORCED    ALL VFS read/write operations MUST be encrypted.
                            Unencrypted disk access is denied.
                            |
Level 2: LOCKDOWN           New task creation is blocked.
                            The in-RAM master key is destroyed, and encrypted
                            VFS access is then refused outright rather than
                            silently attempted with a zeroed key.
                            |
Level 3: IMMUTABLE          Disk writes are completely disabled.
                            Kernel enters read-only mode. Dirty sectors already
                            in the block cache can still be flushed.
```

### Authentication

- User database stored in `/etc/shadow` in a `$v1$` format that records the
  iteration count and a 16-byte per-user salt alongside the derived key
- Passwords are derived with PBKDF2-HMAC-SHA256, not a bare hash. The stored
  iteration count is clamped to a sane range on read, so a tampered `/etc/shadow`
  cannot weaken verification or stall the kernel with an absurd count
- Verification is constant-time over the derived key
- Repeated failures are rate limited per process
- `su` command requires password re-authentication
- UID-based permission model (root = UID 0)
- `/etc/passwd` file is protected from non-root modification (delete, overwrite, rename)

### Disk Encryption

- AES-256-CBC, with the IV stored as the first 16 plaintext bytes of each file
- **The file system key comes from a passphrase entered at boot**, as of v1.1.0.
  Two levels: PBKDF2-HMAC-SHA256 turns the passphrase into a key-encryption key,
  and that unwraps a random data key stored in the superblock's key slot. The
  data key is what encrypts files, which is why changing the passphrase rewrites
  116 bytes instead of every file on the disk
- The key slot holds a 32-byte salt, the iteration count, the IV, the wrapped
  key and an HMAC-SHA256 tag over all of them. A wrong passphrase produces a
  wrong key-encryption key, the tag does not verify, and the disk is refused —
  three attempts, then the machine stops. The tag covers the salt and IV as well
  as the ciphertext, so an attacker holding the disk cannot substitute a salt of
  their own and open it with a passphrase they chose
- **There is no fallback to a built-in key**, and that is deliberate: a disk
  whose key slot was zeroed would then decrypt under a key anybody can extract
  from the kernel binary, which is a downgrade hole written into the format.
  A v2 disk is refused by name instead, the way v0.10.0 refused v0.9.x disks
- A **separate build-time constant** still decrypts the `/bin` images embedded in
  the kernel, and it only ever claimed tamper resistance. Those images are
  decrypted with it once and then written to the disk **re-encrypted under the
  passphrase-derived key** — before v1.1.0 they were stored exactly as the build
  had encrypted them, so every program in `/bin` sat on disk under a key anyone
  with the binary could read
- Changing the passphrase is `setkey` in the shell, root only
- Per-file IVs are derived rather than drawn directly from the entropy pool:
  `HMAC-SHA256(file key, "esdumanOS-iv-v1" ‖ counter ‖ pool bytes)`. The
  monotonic counter makes the input distinct on every call, so two files cannot
  share an IV even on a machine whose entropy sources are worthless; keying the
  derivation with the file key keeps the result unpredictable to anyone without it
- Magic header ("SAFE") and an HMAC-SHA256 tag over the plaintext for integrity

---

## Known Limitations

The following are known constraints of the current implementation. These are documented here for transparency and will be addressed in future releases.

### Resource Limits

| Resource | Limit |
|----------|-------|
| Maximum processes | 16 (`MAX_TASKS`) |
| File descriptors per process | 16 (`MAX_FD_PER_TASK`) |
| Files in directory table | 512 (`MAX_FILES_IN_DIR`) |
| Maximum filename length | 64 bytes (`MAX_FILENAME`) |
| Maximum path length | 256 bytes (`MAX_PATH`) |
| Maximum disk size (FAT) | 16 MB (4096 clusters of 8 sectors) |
| Allocation unit | 4 KB (`FS_CLUSTER_SECTORS`, 8 sectors) |
| Physical memory supported | 128 MB |
| Pipe buffer size | 4 KB (`PIPE_SIZE`) |
| Pipes, system-wide | 16 (`MAX_SYSTEM_PIPES`, shared by named and anonymous) |
| Per-process kernel stack | 8 KB (`KERNEL_STACK_SIZE`) |
| Block cache | 64 sectors, 32 KB (`BCACHE_SIZE`) |
| Kernel log ring | 512 records, ~88 KB (`KLOG_RECORDS`) |
| Longest `alarm()` interval | ~249 days (half a 32-bit tick counter at `TIMER_HZ`) |
| Latest representable timestamp | 2106 (`uint32_t` seconds since the Unix epoch) |

**Timestamps run out in 2106, and v1.0.0 froze that rather than fixing it.** The count is
a `uint32_t` of seconds since the Unix epoch and it is stored in two places that are now
both frozen: `esd_stat_t`'s `st_mtime` and `st_ctime`, which cross the syscall boundary,
and a directory entry's `mtime` and `ctime`, which are on the disk. Widening it breaks
both at once — the entry is exactly 96 bytes and there is a test that says so — and it
would reopen a disk format that v0.10.0 had just finished settling, to move a problem
past the lifetime of the format itself. `esd_time_t` is not the limit here and is
sometimes mistaken for it: its `year` is a `uint16_t`, good to 65535.

### Architectural

- **Single-core only.** SMP data structures are stubbed but not implemented.
- **The kernel is not preemptible.** A task that enters the kernel runs until it
  returns to user mode or blocks of its own accord; only Ring 3 frames are ever
  rescheduled. This is deliberate, and much of the kernel depends on it — the
  block cache, the pipe pool and the ATA driver hold no locks at all, the task
  list is edited without masking interrupts, and the uaccess fault state is a
  single global. Making the kernel preemptible means fixing all of that first,
  starting with the interrupt frame layout, which cannot currently describe a
  Ring 0 frame.
- **A pipeline is four stages at most**, and cannot be backgrounded. An external stage
  costs two tasks — the forked child plus the one `exec()` creates — and `MAX_TASKS` is
  16. Both limits are reported rather than reinterpreted.
- **`/etc` is written only on a fresh disk.** The system files live in the same block as
  the `/bin` tools, which runs when `init.elf` is absent, so an image carried over from an
  earlier version keeps its old `/etc`.
- **`/etc/profile` is a settings file, not a script.** Only `export KEY VALUE` is
  recognised. Running arbitrary commands from it would mean forking and exec'ing before
  the first prompt, and a syntax error in it would be a shell that will not start.
- **The shell tracks at most eight jobs**, and a job of at most four processes. Both
  are the pipeline budget seen from the other end. A ninth job still runs and is still
  collected; it just cannot be named with `%n`.
- **Ctrl-C and Ctrl-Z cannot reach a builtin.** A builtin runs inside the shell, and
  the shell declines both signals so that a keypress at an idle prompt does not end or
  park the session — so `sleep 30` typed at the prompt runs to completion. External
  programs, and every stage of a pipeline, are in their own group and stop as expected.
- **A program that makes no system calls at all is interrupted or stopped late.** The
  default action for an unhandled signal is applied on the way out of a syscall, so a
  task spinning without one keeps its pending signal until it makes its next call. A
  registered handler is delivered at the next context switch either way.
- **There are no set-user-id or set-group-id bits.** They are stored — the mode mask has
  always been `07777` — and nothing consults them, so a program cannot run as anyone but
  whoever launched it. The sticky bit was in the same position until v0.9.4 and is now
  enforced; these two are not, and there is no plan for them: a system with no `x`-only
  binaries and a compiled-in disk key has nothing to gain from privilege elevation it
  cannot also protect.
- **A read asks for both read and search permission on the directory.** Unix lets a
  `0711` directory be searched without being listed, so a caller can open a file whose
  name it already knows. `check_vfs_access()` is not told which of the two its caller is
  about to do, so it asks for both — stricter than Unix, never looser, and no mode this
  system sets for itself distinguishes them.
- **A disk written before v1.1.0 cannot be read, and is refused rather than converted.**
  The format has changed three times and there is no converter for any step. v0.10.0 put
  a partition table in sector 0, where v0.9.x kept the superblock, so a v0.9.x image is
  recognised by its own magic number sitting where the partition table now goes — named,
  refused, and not written to. v1.1.0 added the key slot and moved the format to v3, and
  a v2 disk is refused by its recorded version for a reason worth stating: reading one
  would mean falling back to the compiled-in key, which is a downgrade path an attacker
  could force by zeroing 116 bytes. Anything older than v0.9.0 is refused with less to
  say about it. Every release ships a fresh `disk.img`, and `make run` starts from a
  blank one.
- **There is no group database.** A task's `gid` starts equal to its `uid` and follows
  it, so the group on a file is always its owner's. The field exists because the format
  needs one and because a value taken from the creating task is a better answer than a
  plausible-looking constant, not because groups mean anything yet.
- **A wrapped command line is redrawn correctly only while its first row is on screen.**
  The line editor tracks the wrap itself as of v0.8.4 and moves between rows with `CUU`
  and `CUD`, but both clamp to the top of the visible screen, so a redraw cannot reach a
  row that has scrolled above it. The line is capped at 254 characters and the prompt at
  around thirty, which is four rows out of twenty-four — the case is unreachable in
  practice rather than handled.
- **Ctrl-Z at an idle prompt throws away the line being typed**, exactly as Ctrl-C
  does. The shell ignores the signal, but the read it is blocked in is still cut short
  and has no way to tell which signal cut it.
- **There is no `SIGSTOP`.** Stopping is `SIG_TSTP` and `SIG_TTIN`, and both are
  catchable — which is what lets the shell and init decline them. An uncatchable stop
  would reach init the same way and there would be nothing either of them could do
  about it.
- **`SIGPIPE` and `SIGALRM` are the only signals a program receives without another
  process sending one.** `SIGPIPE` arrives from writing into a pipe nobody is reading,
  and `SIGALRM` from an alarm the program set for itself; `SIGKILL`, `SIGTERM` and
  `SIGTSTP` still have to be sent with `kill` or a key. There is no `SIGCHLD`, `SIGCONT`
  reaches a program only if it registered a handler, and there is no way to block a
  signal rather than ignoring it — the disposition is one of handler, default or ignore,
  with no mask.
- **`SIGTTOU` does not exist.** A background job that *writes* to the terminal still
  does, interleaving its output with whatever the shell is drawing. Only reading is
  stopped, which is the half that made the shell unusable.
- **`lockdown` prints its warning from inside the kernel**, so it goes to the screen
  whatever the calling process's descriptor 1 points at. Every other command that did
  this was moved off it in v0.9.2; this one stayed on purpose. What it prints is not
  output but a red banner announcing a state change to whoever is at the console — the
  same class as the message `halt` prints on the way down — and there is no pipeline that
  wants it. It writes a `klog` record as well, so `dmesg` can say when it happened.
- **FPU state is switched eagerly, and stays that way.** Every context switch saves
  and restores the full 512-byte register file rather than parking it behind CR0.TS
  and restoring it on first use. Lazy switching was on the roadmap as an
  optimisation and has been dropped on purpose: it is the mechanism behind LazyFP
  (CVE-2018-3665), which leaks FPU and SSE register contents across processes
  speculatively, and every major operating system moved back to eager switching in
  2018. One `fxsave` per switch across at most 16 tasks is the cheaper side of that
  trade by a wide margin.
- **`mmap()` is anonymous only.** There is no file backing and no `MAP_FIXED`: the
  kernel picks the address, the pages arrive zeroed, and nothing on disk is behind
  them. Mapping a file would need a page cache, which is a subsystem rather than a
  flag.
- **The program break never comes back down.** `umalloc()` reuses freed blocks but
  does not return the run to the kernel, because a run can only be released from the
  top and the top is rarely the part a program finished with. Allocations of 64 KB or
  more sidestep this by taking their own mapping, which `ufree()` releases exactly.
- **Pages are allocated eagerly.** `brk` and `mmap` both map every page before they
  return, rather than mapping on first touch. A program that reserves a large range
  and uses a little of it pays for all of it.
- **PIO disk access.** ATA driver uses Programmed I/O, not DMA. Single-sector transfers only.
- **No networking.** No TCP/IP stack, Ethernet driver, or socket API.
- **No dynamic linking.** All user-space programs are statically linked.
- **The log is written at checkpoints, not continuously.** `sync`, `halt` and
  `reboot` write `/var/log/kern.log`; a machine that loses power between them
  loses whatever was recorded since the last one. Appending is not possible in
  the current on-disk format — a file is one AES-CBC blob authenticated over its
  whole plaintext — so continuous writing and rotation wait on the format change.
- **`/dev/kmsg` keeps one cursor per process, not per open descriptor.** The
  device interface has no per-descriptor state to hang one on, so two descriptors
  in the same program share a read position. Linux keeps one per open.
- **No ACPI.** Shutdown and reboot use legacy keyboard controller reset.
- **VGA text mode only.** No framebuffer or graphical output.
- **One test is not deterministic, and the cause is not established.**
  `test_time`'s round trip — set the clock to a known time, read it back, compare
  — fails often enough to meet in ordinary use: roughly two full runs in five
  during the session that documented it, not the one-in-fifty first guessed here.
  Sometimes only the date assertion fails, sometimes the time-of-day one beside
  it as well, which means the read can come back wrong in every field rather than
  in the date alone — and that rules out the first theory, a date-register roll
  landing between the write and the read.

  Both assertions read the same structure from one `rtc_read_utc()`, and that
  read is the textbook-correct sequence: wait out the update-in-progress flag,
  read, read again, and accept only two readings that agree. The write path halts
  the update cycle with register B's SET bit before touching anything, and
  `rtc_set_utc()` returning `E_OK` is asserted separately and does not fail. So
  the write is accepted and the value that comes back is not the one written.

  An extra update-in-progress wait after asserting SET was tried and made it
  worse rather than better, which also disproved the theory behind it: with SET
  asserted the chip does not update, so that wait is a no-op and cannot have
  been what changed anything. It was removed. What is written down here is what
  is known — the test is flaky — rather than a mechanism nobody has demonstrated.
  Fixing it properly means either making the assertion deterministic or reading
  QEMU's RTC implementation to find the real interaction.

### Security

- **The passphrase is the whole of the disk's security, and there is no recovery.**
  As of v1.1.0 the file system key is unwrapped from a key slot by a passphrase
  entered at boot, so the disk is genuinely confidential at rest — and nothing
  anywhere holds a copy. A forgotten passphrase is a disk nobody can read, by
  design. There is one key slot, not the eight LUKS gives you, so there is also
  no second passphrase to fall back on.
- **The passphrase protects the disk at rest and nothing else.** Once the machine
  has booted, the key is in RAM and any root process reaches every file through
  the mounted file system. `LOCKDOWN` zeroes the key, which is what that level is
  for.
- **The key for the embedded `/bin` images is still compiled in.** It decrypts the
  programs baked into the kernel image, which are then written to disk
  re-encrypted under the passphrase key. It claims tamper resistance only, and
  anyone with the binary has it — but it no longer opens anything on the disk.
- **Entropy is weak without RDRAND.** The pool is fed by interrupt timing, and
  the only source it credits meaningfully is the keyboard — the PIT is periodic
  and earns nothing, and disk completions are capped at 64 bits for the whole
  boot. On a headless machine with no RDRAND the pool never reaches the threshold
  at which it would claim cryptographic quality, and it reports that honestly
  instead of pretending otherwise. IV and salt *uniqueness* does not depend on
  this; unpredictability does.
- **The cross-boot seed carries distinctness, not quality.** `/var/random-seed`
  is mixed into the pool at boot and credited zero bits, so two boots of the same
  image no longer start from the same state — but a seed written by a pool that
  never reached cryptographic quality does not gain any by being stored. A
  machine with no RDRAND still reports `ENTROPY_WEAK` with a seed loaded, which
  is the honest answer. The seed is also replaced the moment it is read, so a
  machine that loses power before its next checkpoint cannot reuse one.
- **No ASLR, no stack canaries in user space, no W^X for user pages.**
- LOCKDOWN blocks new programs and destroys the master key. It does not restrict
  an already-running shell, so a session that is open when the level is raised
  keeps its terminal.

---

## Roadmap

Near-term priorities for the project, roughly in order:

| Priority | Item |
|----------|------|
| P1 | `mount` and `umount`, so the second partition the table makes room for can be used |
| P2 | Per-mutex wait queues, replacing the global `wakeup_tasks()` sweep |
| P2 | A disk larger than 16 MB: the allocation table is a static `uint32_t[4096]`, and sizing it from the device is what the block layer was the prerequisite for |
| P3 | PCI bus enumeration — neither AHCI nor USB can be written without it, and `io.h` has no 32-bit port access yet |
| P3 | Expanded /dev device drivers |
| P3 | RISC-V port (the Makefile branch exists; `arch/riscv/` does not) |

Deliberately **not** planned: making the kernel preemptible. See the note under
[Architectural](#architectural) — the guarantee that kernel code runs to
completion is load-bearing for the block cache, the pipe pool, the ATA driver and
the uaccess state, and removing the guard without replacing all of that would
trade a documented limitation for silent corruption.

The following were on this list and are done: multi-block SHA-256 with correct
padding; supervisor-only kernel page permissions with `CR0.WP` enabled; address
space teardown on exit with frame reclamation; ELF segment address validation
(plus a fuzzer); BSS zeroing in the bootloader; block cache integration into the
VFS read/write path; salted password hashing, now PBKDF2-HMAC-SHA256; this syscall
reference; moving the shell onto `fork`, which brought concurrent pipelines and job
control in v0.5.2 and `SIGPIPE` in v0.5.3; ANSI escape code support in the terminal,
which landed in v0.8.0 and had been sitting here as a P3 for two releases after it did;
the on-disk format, which grew a superblock, timestamps, an owner group and permission
bits in v0.9.0; enforcing those bits, which retired the last of the name comparisons in
v0.9.1; and setting the clock, which v0.9.2 added along with clearing out the commands
that printed their output from inside the kernel. Three more went in v0.10.0: bounded string
operations in user space, validating the directory table on mount, and the disk growing
past 2 MB - which needed clusters rather than the partition table it arrived with. Two more left this list in v0.9.3: the
entropy seed carried across boots, and `/var/log` — which had been sitting here as a P1
since v0.6.1 did every part of it. The log wraps, it holds records rather than a
transcript of the screen, and it is written out at `sync`, `halt` and `reboot`; the row
outlived the work by five releases. Deriving the disk key from a boot passphrase left this list in v1.1.0, which
also closed the hole under it: the `/bin` images had been stored on disk exactly as the
build encrypted them, so they were readable to anyone holding the kernel binary no matter
what the user's passphrase was. A block device layer left this list in v1.2.0, which the storage work ahead needs and
which arrived with the reason to build it: the file system could not tell a disk it had
failed to read from a disk with nothing on it, and formatted the second. And the sticky
bit on `/tmp`, which v0.9.4 added
alongside the other half of the permission enforcement: the file's own mode. v1.0.0 froze
the syscall ABI, which had been waiting on the disk format rather than on `mount` — a
freeze stops numbers from changing meaning and says nothing about adding new ones, so
`mount` and `umount` can take 68 and 69 whenever they are written.

---

## Contributing

Contributions are welcome. If you are interested in contributing to esdumanOS, please follow these guidelines:

### Getting Started

1. Fork the repository and create a feature branch from `main`.
2. Set up the build environment using the dependency list in the [Building](#building) section.
3. Run the full test suite before submitting changes:
   ```bash
   make test && make fuzz && make && make test_kernel
   ```

### Code Style

- **Language:** GNU C, freestanding (no libc). The build passes no `-std`, so it
  takes the compiler default and relies on GNU extensions throughout — inline
  `asm`, `__attribute__`, statement expressions. It does not compile under strict
  ISO mode and is not meant to. Assembly in NASM syntax.
- **Indentation:** Tabs for indentation, spaces for alignment.
- **Naming:** `snake_case` for functions and variables. `UPPER_SNAKE_CASE` for macros and constants.
- **Comments:** Comment non-obvious logic, and say *why* rather than restating the code. Comments throughout the tree are in English.
- **Headers:** Include the minimal set of headers required. Avoid pulling in `kernel.h` when a specific subsystem header suffices.

### Commit Messages

- Use concise, descriptive commit messages.
- Prefix with the subsystem: `mm:`, `fs:`, `proc:`, `drivers:`, `crypto:`, `tests:`, `build:`.
- Example: `mm: fix backward coalescing in kfree()`

### Pull Request Process

1. Ensure all tests pass (`make test && make test_kernel`).
2. Add or update tests for any new functionality.
3. Update documentation if the change affects user-visible behavior.
4. One feature or fix per pull request.

### Areas Where Help Is Needed

- Expanding the test suite (see Known Limitations).
- Writing user-space programs for `/bin`.
- Documentation improvements.
- Network stack implementation.
- Real hardware testing and driver development.

---

## License

This project is licensed under the MIT License. See [LICENSE](LICENSE) for details.

---

## Credits

**esdumanOS** is developed and maintained by [Esad Furkan Duman](https://github.com/iamfurkann).

### Acknowledgements

- [tiny-AES-c](https://github.com/kokke/tiny-AES-c) — AES implementation reference
- [OSDev Wiki](https://wiki.osdev.org/) — Invaluable resource for OS development
- [GRUB](https://www.gnu.org/software/grub/) — Bootloader
- [QEMU](https://www.qemu.org/) — Emulation and testing

---

<div align="center">

*A kernel built from first principles.*

</div>
