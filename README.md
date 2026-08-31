<div align="center">

# esdumanOS

**A 32-bit x86 operating system kernel written from scratch in C and assembly.**

[![CI](https://github.com/iamfurkann/esdumanOS/actions/workflows/ci.yml/badge.svg)](https://github.com/iamfurkann/esdumanOS/actions/workflows/ci.yml)
![Version](https://img.shields.io/badge/version-1.12.0--beta.1-blue)
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
> new calls continue from the highest assigned number — `SETKEY` took 68 in v1.1.0,
> `PCIINFO` took 69 in v1.4.0, `USBINFO` took 70 in v1.8.0, and the next one takes 71.
> Enforced by
> `tests/kernel/test_abi.c`, which asserts every
> number, errno, flag and security level by literal value. It is a promise about the
> interface, not a claim about the system underneath it: the
> [Known Limitations](#known-limitations) are the same list they were, one test module is
> not deterministic, and one file system is mounted at a time. That is what the
> `-beta.1` is for.

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

**Version:** 1.12.0-beta.1

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
the next one takes 69. `PCIINFO` took it three releases later and `USBINFO` four after
that, which is what turns that sentence from a claim into a habit.

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

v1.3.0 sizes the file system from the device it is on. The directory table and the
allocation table were static arrays — 512 entries and 4096 clusters, whatever the disk —
so a 16 GB stick and a 2 MB image were laid out identically and the first 16 MB was all
either of them could use. Both are allocated at mount now, from what the superblock
records, and the format picks that from the size of the partition: up to 8192 entries and
a little under 1 GB of data.

It is an improvement rather than a fix, and worth being exact about: a large disk mounted
and worked before this, capped. What was actually wrong was that raising the cap alone
would have bought nothing, because 512 entries at 64 KB each is 32 MB of content — the
disk was already within twice what the file system could hold. Moving one without the
other is why both moved together.

**No format change.** The superblock has recorded `max_entries`, `dir_sectors`,
`fat_start`, `data_start` and `total_clusters` since v0.10.0, and `fs_cluster_to_sector()`
has read them rather than the constants. Making the constants ceilings instead of sizes
was enough; a disk written by v1.1.0 or v1.2.0 mounts unchanged, with its own 512 entries.

The risky part was not the allocation but the forty-three loops bounded by the old
constant. Left alone they would have compiled perfectly and read past the end of a
smaller table, so the constant was deleted rather than redefined — `MAX_FILES_IN_DIR` is
`FS_MAX_ENTRIES_CAP` for the two places that mean a ceiling and `fs_max_entries` for the
forty-one that mean a count, and the compiler found every one of them. The same mistake in
reverse, a widening that compiled everywhere and was wrong in eight places, cost v0.9.0
three releases.

v1.4.0 asks the machine what it is. Everything until now assumed: the disk was at
`0x1F0` because IDE controllers were, the keyboard at `0x60` because PS/2 controllers
were, the screen at `0xB8000` because VGA text mode was — each true of the machine this
project is tested on and false of the machine it is aimed at. The PCI bus is enumerated
now, through the configuration ports, with the buses behind bridges walked from a
worklist; `lspci` is what reads it. The enumeration drives nothing, and building it
before the first driver was the point: AHCI and XHCI both begin by asking this exact
question.

It also found a hang that had been in every release ever made. `ata_identify()` carried a
raw wait on the busy bit with no timeout, ninety lines below a bounded helper that
existed the whole time. A bus with no controller reads all ones, and all ones has the
busy bit set — so on a machine with no IDE controller the boot stopped there forever.
QEMU's i440fx always has one, which is why thirty-six releases never saw it; the machine
that finds it is q35, which is the machine class this is aiming at.

v1.5.0 drives the disk that machine actually has. AHCI, and deliberately the smallest
driver that reads and writes a sector: one controller, its first port, one command slot,
one sector per command, polled rather than interrupt-driven — the controller's interrupt
travels a PCI line to an IOAPIC, and there is no IOAPIC here and no ACPI to describe the
routing. It registers as a `blockdev_t` exactly as the IDE driver does, so nothing under
`fs/` learned which of the two it is on. The probe order is IDE first: a machine that had
a disk before still has the same one, which is what made a second storage driver additive
rather than a gamble.

Device memory got a home in the same release — a 16 MB window mapped with caching
disabled, because a control register in a write-back mapping is a write the device never
sees. And `make test_kernel_q35` arrived to run the whole suite on a machine with no IDE
controller at all, where every test that touches storage exercises the new driver without
one assertion being written for it.

v1.6.0 stops the terminal knowing where the screen is. It had written characters into
text-mode video memory since the first release, which is a thing no machine built in the
last fifteen years has. The seam turned out to be six lines rather than the rewrite the
roadmap had budgeted: `tty.c` keeps the cells and the ANSI state, and `console.c` decides
whether a cell becomes a word at `0xB8000` or an 8x16 glyph in a linear framebuffer. The
font was written in this repository, 95 glyphs, ASCII 32 to 126.

v1.7.0 boots on a machine whose firmware has no BIOS, and it cost one line of bootloader
configuration. The roadmap had budgeted a migration to Multiboot 2 and none of it was
needed — MB1 works under UEFI perfectly well; what was missing was GRUB's video driver,
which is built into the core image on BIOS and is a separate module on UEFI. It also
found that `grub-mkrescue` succeeds without the EFI platform files installed and quietly
produces a BIOS-only image, which every ISO this project had ever published was.
`make test_kernel_uefi` runs the whole suite through GRUB under OVMF, and is the only
target in which a bootloader runs at all.

v1.8.0 brings up the bus the keyboard and the stick are both on. XHCI: the controller is
reset, taken from the firmware where the firmware is holding it, given its device context
array and both rings, and then asked to do the smallest thing it can — a No Op command,
whose completion is read back off the event ring. That is the release rather than
ceremony at the end of it: a controller that has been reset and started reports itself as
running with its rings pointed at any memory at all, and only a TRB going out one ring
and coming back the other proves otherwise. `lsusb` arrived with it, so the layer had a
reader from the day it existed.

The roadmap had asked for multi-page contiguous physical allocation and it was not
needed: a ring segment is 4096 bytes and the only placement rule it has is that it must
not cross a 64 KB boundary, which a 4 KB-aligned frame cannot do. `mm/pmm.c` has not been
touched by any of the USB work.

v1.9.0 talks to what is on the bus. Every connected port is reset — which is also what
makes its speed field mean anything, and why v1.8.0 could print "unknown speed" for a
device that was plainly attached — then its device is given a slot and an address and
asked to describe itself. `lsusb` stops being a list of ports and becomes a list of
devices.

The number that release turned on is not in any specification's headline. A context entry
is eight doublewords on every controller ever made; the distance between two of them is
32 or 64 bytes depending on a bit the controller publishes, and on QEMU's those two
numbers are equal. Indexing by `sizeof()` would have been correct on every machine this
project can run on and wrong on a great many it is aimed at, with nothing reporting it —
which is exactly what `ata_identify()` was for thirty-six releases.

v1.10.0 types on a keyboard that is not plugged into a PS/2 port, and that was the last of
the three things standing between this kernel and the hardware it is aimed at. It cost no
change at all to the keyboard driver. `drivers/keyboard.c` is 376 lines with exactly one
that touches hardware, and `keyboard_handle_scancode()` had been split out of it for
testability nine releases before anything needed two backends — so the USB driver
translates HID usages into set-1 scancodes and calls it, and the Turkish layout, AltGr,
the Ctrl fold, Ctrl-D and the arrow sequences all keep working without being touched.

Two decisions in it are worth recording. A boot report says which keys are held rather
than which changed, so each is read as a difference against the last; a driver that
emitted what it saw would repeat every held key a hundred times a second. And the poll
runs directly on the timer interrupt rather than through the bottom-half mechanism
sitting three lines away in the same function — because that mechanism runs from the
scheduler, the scheduler runs only when returning to Ring 3, and the first thing anybody
types on a real machine is the disk passphrase, at a prompt that is a Ring 0 read loop on
the boot path. It would have been unreachable at exactly the moment it is needed, and it
would have passed every automated test in the tree.

v1.11.0 gives the stick a file system. USB mass storage over the endpoints the
controller driver opens: Bulk-Only Transport, which is a 31-byte command wrapper
out, the data, and a 13-byte status wrapper back, with five SCSI commands on top
of it. Sticks register as `usb0` and `usb1` and deliberately do not become the
root - the boot order that made v1.5.0 additive is untouched - and `mount` is
what chooses one. That word is exactly the one `include/blockdev.h` used in
v1.2.0 when it said the second device would arrive with a caller that had to
choose between them; nine releases later it did.

Two sticks rather than one, because that is the machine: booting esdumanOS from a
flash drive and keeping its file system on another means both are plugged in, and
a driver that took whichever enumerated first would have picked the right disk
about half the time. And if the disk the machine booted from holds no file system
this kernel can read, the others get a turn and the one it settles on is named
out loud - without that, an internal SATA disk carrying somebody else's
partitions is refused, correctly and untouched, and then there is no `/bin`, so
no shell, so nowhere to type `mount usb1`. The machine that most needs to be told
which disk to use was the one that could not be told.

The dangerous part was not the transport. The block cache keyed a slot on the
sector number alone, which was correct with one disk and silently wrong with two:
sector 4000 of the root and sector 4000 of the stick would have been one slot,
and a write-back cache would have eventually flushed it to whichever device the
code believed it belonged to. A plumbing fault is loud; that one is not, and it
is what the middle of `tests/kernel/test_mount.c` exists to catch.

v1.12.0 turns the machine off. `halt` ran `cli; hlt`, which stops the processor
and leaves the fans running, and `reboot` wrote to a keyboard controller that a
modern UEFI laptop need not contain - so on the hardware this project is aimed
at, the only way to end a session was the power button.

ACPI is the answer and it needs one thing to begin: the Root System Description
Pointer. On a BIOS machine that can be found by scanning the two legacy areas
below a megabyte. On a UEFI machine it cannot - those areas need not exist, and
the firmware publishes the pointer in the EFI configuration table, which only the
bootloader ever sees. Multiboot 1 has no field to carry it, and that is why
`arch/x86/boot/boot.asm` grew a second header rather than a replacement: three of
the four kernel test targets boot through QEMU's `-kernel`, which reads MB1 and
nothing else. Both specifications enter the same way - the magic in `eax`, the
information structure in `ebx` - so there is one entry point and one branch, and
`mb2_translate()` fills in the MB1 structure the rest of the kernel has always
read. `init_pmm()`, the framebuffer console and the command line parser were not
touched.

What is deliberately absent is the rest of ACPI. There is no AML interpreter; the
one place AML is touched is a byte search for `\_S5_`, and `acpi_parse_s5()` says
so in its own name. No MADT, no IOAPIC, no HPET, no PCI routing - AHCI and xHCI
go on polling exactly as they did. `halt` cuts power and falls back to the old
`cli; hlt` where it cannot, and `reboot` became a ladder: the FADT's reset
register, then port `0xCF9`, then the i8042, then a triple fault. No syscall
number was spent on any of it. 21 and 20 keep their numbers, their parameters and
their names, and the shell's `halt` builtin is unchanged - it simply reaches a
machine that switches off.

The other decision worth recording is how a synchronous read reaches an event
ring whose only reader runs in the timer interrupt. Waiting for a tick to drain
it would have worked and cost ten milliseconds a transfer - three transfers to a
sector, so hours to format a disk. The wait drives the poll itself instead, and
the poll grew a guard so that a tick landing in the middle of one does not step
the same dequeue pointer twice.

It remains a development release, intended for developers, OS enthusiasts, and
anyone curious about kernel internals — not for storing anything you care about.

**What works:**
- Boots via GRUB, initializes all subsystems, and launches a Unix-style shell
- Preemptive multitasking with ELF binary execution
- Encrypted file system with AES-256-CBC
- User authentication and security levels
- 23 user-space programs and 34 shell builtins
- 45 kernel self-test modules and CI pipeline
- Boots on UEFI as well as BIOS, draws to a framebuffer, and types on a USB keyboard

**What to expect:**
- This is not production-ready software
- You may encounter kernel panics, deadlocks, or unexpected behavior
- Resource limits are intentionally constrained (16 processes, 1 GB disk, 128 MB RAM,
  8192 file system entries, 64-character file names)
- No networking, no GUI, no dynamic linking

---

## Features

### Kernel Core

| Component | Description |
|-----------|-------------|
| **Boot** | Multiboot-compliant entry, 16 KB kernel stack, identity-mapped first 16 MB |
| **GDT / IDT / TSS** | 9-entry GDT with Ring 0 and Ring 3 segments, 256-vector IDT with PIC remapping, one TSS for privilege transitions and a second for the double-fault task gate |
| **Syscall Interface** | 67 system calls via INT 0x80, covering process control, job control, file I/O, IPC, security, and device access |
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
| **VFS** | Custom FAT-based file system with flat directory table, CRUD operations, atomic file updates. Sector 0 is an MBR partition table; the file system lives in a partition and its superblock carries a magic number, a format version and the partition-relative geometry it was laid out with. The allocation table counts 4 KB clusters. An unrecognised disk is refused rather than read, a disk that cannot be read is refused rather than formatted, and the directory table is checked against its own invariants before it is believed. The directory table and the allocation table are sized from the device at mount, up to 8192 entries and ~1 GB; entries are 96 bytes each, with an owner, a group, permission bits and creation and modification times |
| **CryptoFS** | Transparent AES-256-CBC encryption layer. Per-file IVs are derived as `HMAC-SHA256(file key, label ‖ counter ‖ pool bytes)`, so they stay distinct even when the entropy pool has nothing to offer; HMAC-SHA256 over the plaintext for integrity |
| **Block Cache** | 64-slot LRU sector cache, write-back, keyed on the device as well as the sector — without which the same sector number on two disks would be one cached slot, and a write-back cache would eventually flush it to whichever device it thought it belonged to. Dirty sectors are written out when 32 slots are outstanding, when any has waited 5 seconds, or on explicit `sync()`. A read the device refuses is not cached, and a sector the device will not take stays dirty so a later flush tries it again |
| **Block Device Layer** | A table of up to four registered devices under the cache, with the sector bounds check in one place rather than in each driver — which matters more with two disks than it did with one, because there are two sector counts to check against and two chances to forget. Devices are found by the name they publish, which is how `mount` is told which one to use, and two may not share one. A driver's errno reaches the file system unchanged, which is what lets the mount path tell a disk it cannot read from a disk with nothing on it |
| **DevFS** | `/dev/null`, `/dev/random`, `/dev/urandom` and `/dev/kmsg` device nodes; the random devices are ChaCha20 keyed from the kernel entropy pool and re-keyed periodically, and `/dev/kmsg` streams log records with a per-process cursor |

### Drivers

| Driver | Description |
|--------|-------------|
| **ATA/IDE** | PIO-mode disk I/O with IRQ-based waiting, 28-bit LBA, single-sector read/write, cache flush. Every wait is bounded, including the one after `IDENTIFY` that was not until v1.4.0 |
| **PCI** | Configuration space through ports 0xCF8/0xCFC, read and written; the buses behind bridges walked from a worklist; up to 32 functions recorded with their class, IDs and base address registers. Memory decoding and bus mastering are enabled for a device a driver claims |
| **AHCI (SATA)** | The disk on a machine with no IDE controller. One controller, one port, one command slot, one sector per command, polled rather than interrupt-driven. Registers as a block device exactly as the IDE driver does, so nothing in the file system knows which of the two it is on |
| **XHCI (USB)** | The bus a machine with no PS/2 controller puts its keyboard on. One controller, one interrupter, one event ring segment, polled. Resets the controller, takes it from the firmware where the firmware is holding it, installs the device context array and both rings, powers the ports and proves the rings work with a No Op command before believing any of it. Then it resets each connected port, gives its device a slot and an address, and reads the device and configuration descriptors — so `lsusb` reports vendors, products and classes rather than only ports. Where one of them is a boot keyboard it selects the configuration, opens the interrupt endpoint and queues a read; the event ring is then drained from the timer tick, a hundred times a second, by a poll that waits on nothing |
| **USB HID Keyboard** | Boot protocol only, which is what makes a report a fixed eight bytes and means no report descriptor has to be parsed. A report says which keys are held rather than which changed, so each one is read as a difference against the last; a rollover report is dropped whole rather than diffed. HID usages are translated into set-1 scancodes and handed to `keyboard_handle_scancode()`, so the Turkish layout, AltGr, the Ctrl fold, Ctrl-D and the arrow sequences all work on a USB keyboard without one line of the PS/2 driver changing. The first boot keyboard on the bus is the one it types on |
| **USB Mass Storage** | Up to two sticks, each presented to the file system as a disk. Bulk-Only Transport - a 31-byte command wrapper out, the data, a 13-byte status wrapper back - and five SCSI commands on top of it: enough to learn the device is there, learn how big it is, and move a sector each way. Each stick carries its own transport frame, tag counter and capacity, reached through `blockdev_t`'s `ctx`. They register as `usb0` and `usb1` and are deliberately **not** made the root; the boot order that made v1.5.0 additive is unchanged, and a stick is chosen with `mount` |
| **PS/2 Keyboard** | IRQ1 handler, US and Turkish layouts, Shift/CapsLock/AltGr, 256-byte ring buffer |
| **Console** | Two backends behind one seam. VGA text mode writes cells to `0xB8000` and drives the hardware cursor; the framebuffer backend draws each cell as an 8x16 glyph into the linear pixel buffer the bootloader hands over, with a software cursor and a shadow so an unchanged cell is not blitted again. The terminal above them knows about neither |
| **VGA Text** | 3 virtual terminals (F1-F3 switching), 80x100 scrollback buffer, status bar, cursor management. 80x25 whichever backend is drawing it |
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
| **Shell** | Login screen, 34 builtins (cat, ls, cd, pwd, mkdir, rm, mv, write, env, export, exec, kill, su, sleep, dmesg, hexdump, help and more; four are deliberately absent from Tab completion and `help`, `panic` among them), four-stage pipelines, output redirection, `&&`/`\|\|` chaining, `$VAR`/`$?`/`~` expansion, line editing with history, and Tab completion that walks its candidates |
| **Programs** | 23 standalone ELF binaries: `sh`, `edit`, `hello`, `echo`, `clear`, `touch`, `rm`, `mv`, `cp`, `free`, `whoami`, `kill`, `grep`, `head`, `wc`, `date`, `stat`, `chmod`, `chown`, `lspci`, `lsusb`, `mount`, `umount` |
| **FHS Layout** | `/bin`, `/dev`, `/etc`, `/home`, `/root`, `/tmp`, `/var` created at boot |
| **Authentication** | Password-protected login, `/etc/shadow` database, `su` for user switching |

### Testing and CI

| Layer | Description |
|-------|-------------|
| **Kernel Self-Tests** | 44 kernel-mode modules: abi, keyslot, blockdev, pci, ahci, xhci, usbkbd, usbmsc, mount, console, string, memory, pipe, VFS, devfs, passwd, security, stress, adversarial, integration, regression, concurrency, paging, PMM, lifecycle, fork, COW, umem, fault, syscall, klog, tty, pgroup, jobctl, kbd, edit, process, signal, reap, ELF, crypto, entropy, bcache, time — plus a Ring 3 payload that exercises the privilege boundary from the unprivileged side. `make test_kernel MODULE=<name>` runs one of them alone |
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
| `grub-efi-amd64-bin` | 2.04+ | The EFI half of the ISO. Without it `grub-mkrescue` still succeeds and quietly produces an image that boots on BIOS only — which every ISO before v1.7.0 was |
| `ovmf` | any | UEFI firmware, for `make test_kernel_uefi`. Not needed to build |
| `qemu-system-i386` | 5.0+ | x86 emulation |
| `qemu-system-x86_64` | 5.0+ | Only for `make test_kernel_uefi`: the firmware is 64-bit even though the kernel is not |
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
    grub-common grub-pc-bin grub-efi-amd64-bin ovmf xorriso mtools libssl-dev python3
```

A minimal Debian netinst does not ship everything the build needs. Add
`make`, `git`, `xxd` (the ELF embedding step calls `xxd -i`), and `bear` if you
want a `compile_commands.json` for your editor.

**Fedora:**

```bash
sudo dnf install gcc nasm make qemu-system-x86 \
    grub2-tools-extra grub2-efi-x64 edk2-ovmf xorriso mtools openssl-devel python3
```

**Arch Linux:**

```bash
sudo pacman -S gcc nasm make qemu-system-x86 \
    grub edk2-ovmf xorriso mtools openssl python
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

**The console is made of pixels as of v1.6.0, and a terminal cannot show it.** The
kernel asks the bootloader for a framebuffer and draws glyphs into it, so `make run`
serves the display over VNC on `:1` — connect to `vnc://<host>:5901` from a machine with
a screen, tunnelling the port over ssh if it is not directly reachable. macOS has a
client built in, under Finder's *Connect to Server*.

Two alternatives, both real. Copy `esdumanOS-v1.12.0-beta.1.iso` and a disk image to a
machine with a display and run them there — the build has to happen on the development
machine but looking at the result does not. Or run `make run_text` and choose the second
entry in the GRUB menu, which sets `gfxpayload=text`: the kernel finds no framebuffer,
stays on its VGA backend, and appears in the terminal exactly as it did before this
release. Most development is not about the screen, and that path is also the only thing
outside the test suite that ever exercises the text-mode fallback.

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
qemu-system-i386 -cdrom esdumanOS-v1.12.0-beta.1.iso -boot d -serial file:kernel_log.txt \
    -drive format=raw,file=disk.img,if=ide,index=0,media=disk -display vnc=:1 -k en-us
```

Which means:
- **`-boot d`** — boot from the CD-ROM, and it is not optional once the disk has been
  formatted. The kernel writes an MBR carrying a valid `0xAA55` signature, because the
  file system uses that signature to recognise its own partition table at mount; the
  446-byte boot area is zero and nothing there was ever meant to run. The BIOS cannot
  tell those two facts apart, so without this it calls the disk bootable, jumps into
  446 bytes of zeros, and stops at `Booting from Hard Disk...`. The first boot of a
  blank disk works because a blank disk has no signature yet.
- **`-display vnc=:1`** — the screen is served on port 5901 rather than drawn in a
  window, because the machine this is developed on is headless and because a terminal
  cannot render a framebuffer at all. `make run_text` uses `-display curses` instead,
  which only shows anything if the GRUB menu's text-mode entry was chosen.
- **`-k en-us`** — VNC carries key symbols rather than scancodes, so without a keymap
  QEMU has to guess at the layout. That guess is how a disk passphrase typed at the
  unlock prompt arrives as a different string than the one that was set.
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
    -cdrom esdumanOS-v1.12.0-beta.1.iso \
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

### Booting the way a modern machine boots

As of v1.7.0 the ISO carries an EFI boot path as well as a BIOS one, so it starts
on a machine whose firmware has no BIOS to fall back to. Under QEMU that needs
the firmware and the 64-bit binary — the firmware is 64-bit even though the
kernel is not:

```
cp /usr/share/OVMF/OVMF_VARS_4M.fd /tmp/ovmf_vars.fd
qemu-system-x86_64 -M q35 \
    -drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
    -drive if=pflash,format=raw,file=/tmp/ovmf_vars.fd \
    -cdrom esdumanOS-v1.12.0-beta.1.iso \
    -drive format=raw,file=disk.img,if=ide,index=0,media=disk \
    -serial file:kernel_log_uefi.txt
```

The variables file has to be a writable copy; the one in `/usr/share` is not. And
this is the configuration in which all three of the last releases are used at
once: there is no IDE controller, so the disk arrives through the SATA driver
v1.5.0 added, found by the bus walk v1.4.0 added, and drawn on a framebuffer by
the console v1.6.0 added, because UEFI has no text mode to draw on instead.

`make test_kernel_uefi` runs the whole self-test suite through this path.

### Booting a machine with no IDE controller

Everything above runs on QEMU's default i440fx, which carries a PIIX3 IDE
controller whether or not a disk is attached to it. The machine this project is
aiming at does not have one at all. Until v1.4.0 that was not a missing feature
but a hang — the wait for the busy bit after `IDENTIFY` had no timeout, and an
undecoded port answers with all ones, which has that bit set — and until v1.5.0
it was a system that could say what was holding its disk and not read it.

```
qemu-system-i386 -M q35 -cdrom esdumanOS-v1.12.0-beta.1.iso -boot d \
    -serial file:kernel_log_q35.txt \
    -drive format=raw,file=disk.img,if=ide,index=0,media=disk -display curses
```

The drive line is the same one the i440fx invocation uses. `if=ide` asks for the
machine's IDE interface, and on q35 that interface is the ICH9 AHCI controller —
so the same request reaches a different controller, and the kernel takes the disk
through the SATA driver instead.

This is now a working system: a passphrase prompt, a login, a shell, and files
that are still there after `reboot`. The log should show the PCI inventory, a
line saying no IDE controller answered on the primary bus, and the AHCI driver
reporting the port and capacity it found. The CD-ROM sits on the same controller
as the disk, so a boot that reaches the shell is also evidence that the driver
told the two apart by their device signature.

Without a disk it still stops, and stops legibly: the controller is found, no
port has a disk on it, and the file system refuses to mount rather than
formatting something it cannot read.

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

# Boot kernel in self-test mode: 44 kernel-mode modules, then a Ring 3 payload.
# The machine is i440fx, so the disk is reached through the IDE driver.
#
# Every kernel target below also gets an xHCI controller with a USB keyboard and
# a USB mouse on it, from one variable in the Makefile. Neither i440fx nor q35
# provides a USB controller by default, so before v1.8.0 there was none on any of
# them; they are given the same devices so that the totals stay comparable.
make test_kernel

# The same suite with one flag changed, on a machine with no IDE controller,
# where the disk is behind a SATA controller instead. Everything that touches
# storage exercises the AHCI driver without an assertion being written for it.
# It must report the same assertion total as the run above.
make test_kernel_q35

# The same suite again, this time booted the way a real machine boots it: an ISO,
# GRUB, UEFI firmware, and a framebuffer console. The only target that does not
# pass -kernel, and therefore the only one in which a bootloader runs at all.
# Needs ovmf and grub-efi-amd64-bin; it says so if they are missing.
make test_kernel_uefi

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
| `test_abi.c` | The frozen v1.0.0 interface, asserted by literal value: all 67 syscall numbers, the 34 error codes, the `exec`/`wait`/`lseek`/`klog_ctl` constants, the signal numbers and the security levels — plus that each retired number (11, 28, 30, 31, 32, 99) is still answered with `E_NOSYS`. This row said "all 62 numbers ... and that 68 is still free" for three releases after `SETKEY` took 68 |
| `test_console.c` | Both console backends, against an array in RAM rather than a screen: that the framebuffer info sits where the Multiboot specification puts it, that a glyph is drawn pixel for pixel from the font, that foreground and background come out of the attribute byte, that a byte outside the font is drawn as a fallback box, that a cell the buffer does not have is not drawn at all, that a cell redrawn with what it already holds is skipped and one whose contents changed is not, that the cursor is painted over its cell and taken away again, and that a pixel format the backend cannot draw is refused without changing which backend is current |
| `test_ahci.c` | Device memory and the SATA driver, asserted so that the same assertions reach the same verdict on a machine that has an AHCI controller and one that does not: the four structures the controller reads out of memory are the sizes it indexes them by and fit one page with every alignment satisfied, a device mapping preserves its offset within the page and carries the cache-disable bit (read back out of the page table, because nothing else can see it), `pci_enable_device()` sets the two bits it is asked for and leaves the rest of the command register alone, and exactly one of the two storage drivers owns the disk — the one the bus says this machine has |
| `test_xhci.c` | The USB controller, the arithmetic that decided how it is fed, and the bytes the devices on it chose. A TRB and a segment table entry are the sixteen bytes the hardware indexes them by; a ring segment is exactly one page rather than merely smaller than one; the three structures sharing a frame fit it without overlapping and each sits on the 64-byte boundary its register wants; and a frame the allocator actually handed out holds a segment without crossing the 64 KB boundary that is the only placement rule a segment has — which is why `mm/pmm.c` has never been touched for this driver. The context stride is asserted to come from the controller's CSZ bit rather than from `sizeof()`, and the placement is checked at both 32 and 64 bytes, including the one no machine here uses: a device context and an input context each fit a frame at either stride and together do not at 64, which is the whole reason they get a frame each. Then the two register fields that are not where they look — the scratchpad count, whose halves are at 25:21 and 31:27 and are not adjacent, and the PORTSC write mask, which is what stands between a read-modify-write and switching off every working port. Then the walk over a configuration descriptor, driven with buffers made up on the spot: one that parses, one whose descriptor claims zero length and is refused rather than stepped over, one that does not begin with a configuration, and one cut short by the transfer. Then the hardware: a controller found by its programming interface rather than by class, the controller running, a device given a slot and an address and answering with a vendor and a product, and its speed being a named one — which it only is after the port reset this release added |
| `test_usbmsc.c` | Bulk-Only Transport, and the disk it presents. Half of it is arithmetic about two structures and that half matters more than it looks: a command wrapper is 31 bytes and a status wrapper 13, and those are fields of the protocol rather than properties of the C - a device handed 32 bytes where it expects 31 stalls the endpoint, and the recovery for that is a reset this driver does not implement. So the sizes are asserted, the command block is asserted to start at byte 15, and the two signatures are asserted as the ASCII they are rather than against the constants they are defined as. Then the hardware: a stick was found and registered under the name `mount` uses, it reports a capacity and 512-byte sectors, it is **not** the root - the boot order decides that - a sector reads back through the whole transport, and a sector past the end is refused before the device ever sees it. Then the second stick, which is why this driver has a table at all: both were brought up rather than only the first, each registered under a name and a `ctx` of its own, and **their capacities differ** - the test bench gives the two images deliberately different sizes, because a driver keeping the capacity in a single static would register the second stick with the first one's size and produce a disk that mounts and lies about where it ends |
| `test_mount.c` | Two disks, and the cache that has to tell them apart. The registry first: a device is findable by the name it published, a name nothing answers to finds nothing, a second device may not take a name already in use, registering the same device twice is simply nothing to do, and one that cannot read is refused at the table rather than at the first mount. Then the assertion this release turns on - the same sector number written on two devices holds two different things, and each reads back what was written to it rather than to the other, which before the cache carried a device would have been one slot and a silent corruption. Then the root moved to another disk, detached entirely as `umount` leaves it, and put back where the suite found it. Before any of the writes, one assertion that the sector they use exists on *both* disks - it did not, once, and a sector past the end of a device leaves a dirty slot the cache can never place, which is not a failed assertion here but an eviction path that stops working for every module after this one |
| `test_acpi.c` | The tables, the sleep values, and the offsets into the FADT - almost all of it against tables built in the module rather than against the machine, because the cases worth asserting are the ones QEMU never produces. A checksum that does not hold is refused, and so is the *second* one, which only an ACPI 2.0 pointer has and which covers the half carrying the XSDT address. A declared length past the thirty-six byte copy the kernel keeps is refused rather than summed. `\_S5_` introduced by NameOp yields both sleep types shifted into place, the same four characters inside a string yield nothing - acting on that match would send an arbitrary value to a hardware register - and the walk is cut short at *every* offset in the block, not one, because every step is taken over bytes the firmware chose. Then the layout: four of the FADT offsets were wrong on the first attempt, each reading a neighbouring field and each returning a number that looked like an answer, so they are checked against the specification's own arithmetic - Flags is a doubleword, the reset register a twelve-byte generic address, and the fields from Flags to X_DSDT tile without a gap. One assertion asks the machine, and it has no branch in it, because an assertion with two arms reports a different count on different targets |
| `test_usbkbd.c` | HID boot reports and the scancodes they turn into, almost all of it without a controller, a device or a register read: `usbkbd_report()` is handed eight bytes and told nothing about where they came from, so eight made up here are indistinguishable from eight a keyboard sent. The table is asked directly for a letter, for a navigation key that must carry its prefix or arrive as its keypad twin, for right alt which has to become AltGr or the Turkish layout loses a third of its characters, and for a usage set 1 cannot express — which must translate to nothing rather than to something wrong. Then the diff, which is where the shape of this protocol sets its trap: a report says what is held rather than what changed, so a key present in two reports must be pressed once, and a driver that emitted what it saw would repeat every held key a hundred times a second. A release types nothing and is therefore invisible at the ring, so it is proved through a modifier instead — a shift whose break code went missing would leave every letter after it capitalised. A rollover report is dropped whole and, the assertion that matters, leaves the previous report undisturbed. And one key end to end: Up arrives as the escape sequence xterm sends, through a driver that was never told about USB |
| `test_pci.c` | The bus walk and what it recorded: that something answered and that the count fits the table, that the host bridge reports a vendor while an address nothing decodes reads back all ones and is not stored as a device, that the stored vendor and class match a fresh read of the same registers rather than a consistent misreading of them, the lookups in both directions including one-past-the-end and a negative index, that no function above zero was recorded unless function zero announced itself as multifunction, and that enumerating twice describes the machine once |
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
| `test_bcache.c` | Cache hits, and the write-back policy: volume bound, time bound, `sync()`. Then what the cache does when the device underneath says no, in both of the two ways it can. A device having a bad moment: the read is reported rather than swallowed, the failed read is not cached so the next one asks again, the write is taken and the failure surfaces at the flush, the sector stays dirty - and then the device relents and the sector it was holding is finally written, which is what keeping it was for. A device that could never have taken it: a sector past the end of the disk is accepted by the cache, reported by the flush, and **dropped**, because no later attempt could answer differently. The last of those is asserted by the cache still handing out slots afterwards, which is the assertion that fails if the slot was pinned - a slot nothing can write is a slot nothing touches, so it sinks to the bottom of the LRU order and takes every later eviction with it |
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
|   |   |-- syscall.c                Dispatcher, 64 system calls
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
|   |-- pci.c                        PCI bus enumeration and configuration access
|   |-- acpi.c                       ACPI tables, and the one transition: power off
|   |-- ahci.c                       SATA disk driver (polled, one port)
|   |-- xhci.c                       USB controller: rings, ports, devices, endpoints
|   |-- usbkbd.c                     HID boot reports into set-1 scancodes
|   |-- usbmsc.c                     USB mass storage: Bulk-Only Transport, SCSI
|   |-- console.c                    Console backends: VGA text and framebuffer
|   |-- console_font.c               8x16 glyphs, ASCII 32-126
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
|       |-- lspci.c                  List the PCI devices found at boot
|       |-- lsusb.c                  List the USB controller and its ports
|       |-- mount.c                  Choose which block device the file system is on
|       |-- umount.c                 Unmount the file system
|       +-- stat.c                   Show a file's size, type, owner, mode and times
|
|-- include/                         53 header files
|   |-- kernel.h                     Master header, and where the version lives
|   |-- types.h                      Integer type definitions
|   |-- syscall.h                    67 syscall number definitions
|   |-- process.h                    Process control block, scheduler API
|   |-- fs.h                         VFS structures, file operations
|   |-- stat.h                       esd_stat_t and the lseek origins
|   |-- paging.h                     Virtual memory constants
|   |-- entropy.h                    Entropy pool API and quality contract
|   +-- security.h                   Security level enumeration
|
|-- tests/
|   |-- kernel/                      45 kernel-mode test modules + framework
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

The kernel exposes 67 system calls through `INT 0x80`. The syscall number is passed in `EAX`.

**These numbers are frozen as of v1.0.0.** None of them will change value or meaning. The
numbers 11, 28, 30, 31, 32 and 99 are retired and will never be reused — they held calls
that were removed, a reservation for a crypto API that was never designed, and `YIELD`
before it moved to 67 — and new calls continue from the highest assigned number, which is
why `SETKEY` took 68 in v1.1.0, `PCIINFO` took 69 in v1.4.0, `USBINFO` took 70 in v1.8.0,
and the next call takes 71.
Everything from 200 up is
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
| 69 | `PCIINFO` | Render the PCI inventory the kernel took at boot into `ebx`, capacity in `ecx` (root only). Reports what the enumeration recorded; it does not re-read configuration space |
| 70 | `USBINFO` | Render the USB controller, its ports and the devices addressed on them into `ebx`, capacity in `ecx` (root only). A snapshot taken at boot, like `PCIINFO`: there is no hotplug behind it. A machine with no xHCI controller gets one line saying so and a successful return |
| 71 | `MOUNT` | Move the file system to the block device named in `ebx` (root only). With `ebx` zero it asks instead of setting: `ecx` is a buffer and `edx` its capacity, and the registered devices are rendered into it with the mounted one marked. **One file system at a time** — this mounts a device *instead of* the current one, which is the choice `include/blockdev.h` said the second device would arrive needing |
| 72 | `UMOUNT` | Unmount the file system (root only). Flushes and drops what the cache holds for that device, then detaches it. Refused with `E_BUSY` while any process has a file open. Takes no argument, because there is one file system to unmount |

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
| Files in directory table | Sized from the disk at mount, up to 8192 (`FS_MAX_ENTRIES_CAP`). A disk written before v1.3.0 holds 512 |
| Maximum filename length | 64 bytes (`MAX_FILENAME`) |
| Maximum path length | 256 bytes (`MAX_PATH`) |
| Maximum disk size (FAT) | ~1 GB (262144 clusters of 8 sectors, `FS_MAX_CLUSTERS_CAP`). A larger device is used up to that and says so |
| Allocation unit | 4 KB (`FS_CLUSTER_SECTORS`, 8 sectors) |
| Physical memory supported | 128 MB |
| Pipe buffer size | 4 KB (`PIPE_SIZE`) |
| Pipes, system-wide | 16 (`MAX_SYSTEM_PIPES`, shared by named and anonymous) |
| Per-process kernel stack | 8 KB (`KERNEL_STACK_SIZE`) |
| Block cache | 64 sectors, 32 KB (`BCACHE_SIZE`) |
| PCI functions recorded | 32 (`PCI_MAX_DEVICES`). A machine with more gets the first 32 and a log line saying the list stops there |
| USB ports recorded | 32 (`XHCI_MAX_PORTS`). A controller with more gets the first 32 and says so in `lsusb` as well as the log |
| USB devices addressed | 8 (`XHCI_MAX_DEVICES`). Each costs four frames — a device context, an input context, a control transfer ring and a descriptor buffer — so this is a memory budget as much as a table size. A ninth attached device is reported as a connected port with no device line under it |
| Block devices registered | 4 (`BLOCKDEV_MAX`). An IDE disk, a SATA disk and a stick or two is everything this kernel can currently produce. Two may not share a name, because the name is how `mount` is told which to use |
| File systems mounted at once | 1. `mount` changes which device carries the file system rather than attaching a second one — holding two means giving every directory id a file system to belong to, and every one of them is a bare `int` that crosses the frozen syscall boundary |
| USB keyboards driven | 1. The first boot keyboard found wins, the way the SATA driver takes the first port with a disk on it. It costs a fifth frame, for its interrupt transfer ring; the reports themselves land in the descriptor buffer, which enumeration has finished with by then |
| USB events per timer tick | 16 (`XHCI_POLL_MAX_EVENTS`). The poll runs inside IRQ0, so it takes what is there up to a limit and leaves the rest for the next tick rather than holding the interrupt handler |
| USB device slots enabled | 32 (`XHCI_MAX_SLOTS`). Nothing uses one yet; the number sizes the device context array, which is programmed before the controller runs |
| USB scratchpad buffers | 64 (`XHCI_MAX_SCRATCHPAD`). A controller asking for more is refused rather than half-served, since an array the hardware believes is longer than it is gets read past its end |
| Device register window | 16 MB at `DEVICE_WINDOW_BASE`, allocated by a bump pointer with no free. A driver's registers are mapped once at boot and held for the life of the system; the AHCI driver uses 12 KB of it and the XHCI driver 76 KB — 64 KB of registers and three 4 KB pages of rings and tables. Scratchpad buffers, where a controller asks for any, spend a page of window each even though the mapping is dropped once they are zeroed, because the bump pointer does not rewind |
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
- **The PCI enumeration is a snapshot taken once, on the boot path.** Nothing
  rescans and there is no hotplug. Of everything it finds, two classes are
  driven: a SATA controller, as of v1.5.0, and an xHCI controller, as of v1.8.0.
- **The SATA driver is the smallest one that reads and writes a disk.** It polls
  rather than taking interrupts — the controller's interrupt travels the PCI
  interrupt line to an IOAPIC, and there is no IOAPIC here, no ACPI to describe
  the routing and an interrupt dispatcher that is a hand-written chain of
  comparisons. It uses the first SATA controller, its first port with a disk, one
  of the 32 command slots, and moves one sector per command. Multi-sector
  transfers are the hardware's to give and the block interface's to ask for:
  `blockdev_t` reads and writes a sector at a time, and changing that is a
  decision about every driver rather than about this one.
- **Two kinds of USB device are driven: the keyboard and the disk.** A mouse is
  still enumerated and named and never spoken to, because this kernel has no
  pointer. The keyboard is boot protocol only — a fixed eight-byte report, which
  is what lets the driver skip parsing a report descriptor — and the first one
  found is the one it types on. There are no LEDs: Caps Lock is tracked inside
  the kernel and the lamp on the keyboard is never told. The device list is still
  a snapshot taken at boot, with the same absence of hotplug the PCI inventory
  has.
- **The USB disk is Bulk-Only Transport and five SCSI commands, and no more.**
  One logical unit, one command outstanding, one sector per transfer — the shape
  of `blockdev_t` rather than of the hardware. A device reporting a sector size
  other than 512 is refused by name rather than read with the wrong arithmetic,
  which would produce a disk that mounts and is wrong everywhere. A device larger
  than `READ CAPACITY(10)` can describe is refused too, rather than wrapped. The
  two other transports the specification defines are not implemented, and there
  is no reset recovery: a stalled endpoint ends that disk's usefulness until the
  next boot. Two sticks is the ceiling, and it is a stated number rather than a
  memory condition found at boot, as every other table here is.
- **A device behind a USB hub is invisible.** The driver walks the controller's
  root hub ports and stops there. A hub plugged into one of them enumerates as a
  device like any other and nothing underneath it is ever reached, so a stick in
  a hub, a dock, or a monitor's USB ports does not appear — it has to go into a
  port on the machine itself. Nothing reports this as an error, because from the
  root hub's side there is no error: there is a device on the port, and it was
  enumerated.
- **`mount` on a genuinely blank device formats it.** It is `init_fs()`'s
  blank-disk path reached through a second caller, and it is how a new stick is
  made usable — there is no other way to prepare one today. A device that is
  *not* blank is refused untouched, which is what keeps another operating
  system's disk safe: a partition table this kernel does not recognise is a disk
  it will not write to. Whether there is anything there at all is what tells the
  two cases apart.
- **One file system is mounted at a time.** `mount` moves it to another device
  rather than attaching a second one, which is what `include/blockdev.h` asked
  for in v1.2.0 when it said the next device would arrive with a caller that had
  to *choose between* them. Holding two at once means giving every directory id a
  file system to belong to, and every one of them is a bare `int` that crosses
  the frozen syscall boundary; that is v1.12.0. `umount` leaves the system with
  no file system at all, which the command says out loud.
- **The USB keyboard is read from the timer interrupt, not from an interrupt of
  its own.** A hundred polls a second, so a keystroke is seen within ten
  milliseconds of arriving; the controller's own interrupt would need an IOAPIC
  and ACPI routing, neither of which exists here. The poll waits on nothing and
  drains at most sixteen events before returning, because it runs inside IRQ0.
  It does nothing at all until enumeration has finished — during boot the event
  ring has one owner and it is not the timer.
- **A PCI device the firmware places above 4 GB cannot be used at all.** There
  is no PAE, so a physical address in this kernel is 32 bits wide. The xHCI
  driver refuses such a controller and says so in the log rather than mapping
  the low half of an address that means something else — which is a case that
  only appears under UEFI, because OVMF has an aperture above 4 GB to put
  64-bit BARs in and SeaBIOS has none. `make test_kernel_uefi` turns that
  aperture off with OVMF's own `X-PciMmio64Mb=0`, the same accommodation
  firmware makes for any 32-bit operating system. Relocating a BAR from inside
  the kernel would need BAR sizing — a write to configuration space this tree
  has never made — and a search for a free range, so it waits for a machine
  that actually needs it.
- **HID usages that set 1 has no code for are dropped.** Translating through
  the PS/2 representation is what makes the Turkish layout, AltGr and the
  navigation sequences work without being written twice; the price is Pause and
  PrintScreen, which set 1 encodes as multi-byte sequences this kernel has never
  acted on, and the handful of usages a modern keyboard has that a 1981 one did
  not.
- **Three paths in the xHCI driver cannot be exercised on any machine available
  to this project.** QEMU's controller publishes no USB Legacy Support
  capability, so the handoff that takes the controller away from firmware finds
  nothing and returns — on a UEFI machine that firmware has been driving the
  controller to read its own boot keyboard, which is exactly the machine the code
  exists for. QEMU asks for no scratchpad buffers, so the allocation that hands a
  controller its private pages never runs. And QEMU's keyboard and mouse are
  full-speed devices whose endpoint zero takes 8-byte packets, which is the size
  the driver opens it at, so the Evaluate Context that would correct a device
  answering 16, 32 or 64 never issues. All three are written from the
  specification and all three say so where they are written.
- **The 64-byte context layout is arithmetic here, not experience.** How far
  apart the controller places two context entries is a bit it publishes, and
  QEMU's says 32. The driver reads the bit rather than assuming, and
  `tests/kernel/test_xhci.c` checks the placement at both strides — but no
  machine available to this project has ever laid a context out at 64 bytes.
- **Device mappings are never reclaimed.** Registers and DMA buffers are mapped
  into a 16 MB window by a bump pointer with no free, because a driver in this
  kernel is loaded at boot and never unloaded. A free list would be a structure
  with no caller.
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
- **ACPI is read for one purpose and no other.** Enough of it to find the FADT,
  read the PM1 control ports, and pull `SLP_TYP` for S5 out of the DSDT — which
  is what `halt` needs and the whole of what this kernel understands. There is no
  AML interpreter: the DSDT is searched for `\_S5_` as bytes, and a firmware that
  describes it any other way is one this kernel cannot power off. It says so in
  the log at boot rather than at the moment somebody types `halt`. No MADT, no
  IOAPIC, no HPET, no PCI interrupt routing, and no sleep state other than S5.
- **A firmware that hands over no RSDP leaves the machine unable to power off.**
  Multiboot 2 carries the pointer and Multiboot 1 does not, so a machine booted
  through the MB1 menu entry — or by any loader that does not pass the tag — falls
  back to scanning the legacy areas, which works on a BIOS machine and cannot on a
  UEFI one. `halt` then does what it did before this release: stops the processor
  with the power still on, and says which of the two it is doing.
- **No Secure Boot.** The EFI image in the ISO is unsigned, so a machine with
  Secure Boot enabled refuses it. Turning it off in firmware setup is the only
  answer this release has.
- **Both multiboot headers, and both stay.** The binary carries an MB1 header and
  an MB2 header, and the bootloader reads whichever it understands. MB1 is not a
  compatibility shim being tolerated: three of the four kernel test targets boot
  with QEMU's `-kernel`, which reads MB1 and nothing else, so it is the path most
  of this project's evidence comes from. MB2 exists for the one thing MB1 cannot
  carry, the ACPI pointer. What MB2 also brings and nothing yet reads is the EFI
  system table and the EFI memory map; the memory map the kernel uses is still
  the translated MB1 one.
- **The console is 80x25 whatever the screen is.** What the framebuffer backend
  decides is how large those cells are drawn: the largest whole-number scale that
  still leaves all of them on the display, centred, with black around it. At
  1024x768 that is one and the console is 640x400; at the 1920x1080 a laptop
  panel is likely to report, it is two. Whole numbers only, because a bitmap
  glyph scaled by anything else has to decide what to do with half a pixel and
  every answer looks worse than the small version. Growing the terminal itself
  means changing the size of three scrollback buffers in `drivers/tty.c`, which
  is a decision about the terminal rather than about the screen.
- **The font covers ASCII 32 to 126 and nothing else.** Ninety-five glyphs, and
  anything outside them is drawn as a hollow box. Nothing else can be typed: the
  Turkish keyboard layout maps every key to plain ASCII, so `ğ` arrives as `g`.
- **32 bits per pixel only.** A framebuffer in any other format is refused and
  the console stays in text mode, which on a machine already in a graphics mode
  means a blank screen and a line in the log saying why.
- **The framebuffer is mapped uncached, not write-combining.** Setting up the
  page attribute table is separate work and nothing has measured a reason for it;
  the shadow buffer already keeps a repaint down to the cells that changed.
- **Output before the framebuffer is mapped goes only to the serial port.** The
  mapping needs paging, so everything printed before that is written to text-mode
  video memory that a machine in a graphics mode does not display. Nothing is
  lost: the terminal's cell buffers are filled the whole time and the first
  repaint after the mapping shows all of it at once.
- **A terminal cannot display a framebuffer.** QEMU's curses front end renders a
  text-mode screen as characters and answers a graphics mode by printing its
  dimensions and nothing else. See [Running](#running) for what to use instead.
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

As of v1.5.0 this is a route rather than a list of wishes, because the
destination is close enough to name. **2.0.0 is the release that boots on a real
machine, from a USB stick.** Everything between here and there removes one of the
things that stop it.

There were three and all three are gone. The screen was a framebuffer while
`drivers/tty.c` wrote characters to `0xB8000`; v1.6.0 put a seam between them.
The machine booted UEFI while this kernel was loaded by GRUB under BIOS; v1.7.0
shipped an ISO that boots either way. The keyboard was on USB while
`drivers/keyboard.c` read port `0x60`; v1.8.0 brought the bus up, v1.9.0 learned
to talk to what is on it, and v1.10.0 opened the endpoint the keys come out of —
without changing one line of the PS/2 driver.

And the requirement behind them - that a machine booted from a stick be able to
read the stick - is met as of v1.11.0. v1.12.0 added the other half of a usable
session: a machine that can be switched off at the end of one.

What stands between here and 2.0.0 is no longer a missing capability but the
packaging: one stick that carries both the EFI boot path and a file system this
kernel can mount. Two sticks work today, and one is closer than it looks -
`load_partition()` already walks all four MBR entries looking for a partition of
its own type and steps over the ones it does not own, so an EFI system partition
and an esdumanOS partition can sit on the same stick. What is missing is the
recipe and the build step that produce one, not a capability in the kernel.

| Release | What it removes |
|---------|-----------------|
| ~~v1.8.0~~ | **Done.** The bus underneath both of those. XHCI: the command and event rings, port enumeration, and `lsusb`, so it arrived with a reader rather than as a layer nothing calls |
| ~~v1.9.0~~ | **Done.** Talking to what is on the bus: port reset, Enable Slot, Address Device, control transfers, descriptors. `lsusb` names devices rather than ports |
| ~~v1.10.0~~ | **Done.** A keyboard on a machine with no PS/2 controller. HID boot protocol, an interrupt endpoint, and the event ring polled from the timer tick. `drivers/keyboard.c` did not change: it has one line that touches hardware and `keyboard_handle_scancode()` had been the seam for nine releases |
| ~~v1.11.0~~ | **Done.** The stick as a real disk. USB mass storage, and `mount`/`umount` — the caller `include/blockdev.h` has been waiting for since v1.2.0, in the sense that header actually used: one that *chooses between* devices |
| ~~v1.12.0~~ | **Done.** A way to turn the machine off. ACPI, and the Multiboot 2 header it needs: the ACPI pointer is not reliably findable by scanning on a UEFI machine, and MB1 does not carry it. Added *alongside* MB1 rather than replacing it, because three of the four test targets boot through QEMU's `-kernel`, which reads an MB1 header. No syscall number was spent: `halt` keeps its number and finally means what people mean by it |
| v1.13.0 | **Two file systems at once.** The mount in v1.11.0 changes which disk carries the one file system; holding two means every directory id needs a file system to belong to, and every one of them is a bare `int` that crosses the frozen syscall boundary. Measured while v1.11.0 was being written: 466 references across `fs/`, `kernel/` and the tests. It is a release of its own and gets one. It moved behind ACPI because auditing `load_partition()` showed it is not what 2.0.0 is waiting on — shutting the machine down is |
| v2.0.0 | The machine. Named, as only major versions are |

XHCI was split from the drivers above it deliberately, and then split again. It
is the most complicated controller on the platform, it can only be developed
against an emulator here, and v1.4.0 established what works: bring the bus up
with a tool that reads it, then write the driver in the release after. PCI
arrived with `lspci` and AHCI came next; USB arrived with `lsusb`, which then
learned to name devices, and HID comes after that. The second split cost the
roadmap a release and was taken on size: `drivers/xhci.c` was 944 lines after
v1.8.0 with only the bus in it, and doing enumeration and HID together would have
put two subsystems into one release in the file least able to afford it.

The v1.8.0 row above used to say the release needed multi-page contiguous
physical allocation, and it did not. A ring segment is 4096 bytes and the only
placement rule it has is that it must not cross a 64 KB boundary, which a
4 KB-aligned frame cannot do; the device context array, the segment table and the
scratchpad array together use 3584 bytes of one more frame; and the scratchpad
buffers, which are the case that looks like it needs contiguity, do not, because
the array holds each buffer's address separately. `mm/pmm.c` was not touched.
That is the third roadmap row in four releases to overstate what its release
needed — v1.5.0's said the same thing about AHCI and v1.6.0's and v1.7.0's both
called for Multiboot 2 — and the arithmetic is now asserted in
`tests/kernel/test_xhci.c` rather than argued in a comment, so the next person to
raise `XHCI_MAX_SLOTS` past the room there is for it finds out from a failing
test.

**2.0.0 ships as a beta too**, and that was decided along with the rest of this
table rather than deferred again. Running on hardware is a claim about reach, and
this project has spent several releases learning that reach and correctness are
different things — v1.4.0 found a hang that had been in every release ever made,
v1.5.0 found a regression test that had been asserting the wrong thing for four
years, and v1.7.0 found that every ISO ever published booted on BIOS only. The
suffix comes off when somebody decides it should.

Not on the route, and waiting behind it: `su` with the semantics it claims — it
takes a username and ignores it, which is a lie rather than a hole, since the
password it asks for is still root's. Also per-mutex wait queues in place of the
global `wakeup_tasks()` sweep, variable cluster sizes for a device larger than
the ~1 GB a 1 MB allocation table describes at 4 KB clusters, expanded `/dev`
nodes, and the RISC-V port whose Makefile branch exists while `arch/riscv/` does
not.

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
what the user's passphrase was. Sizing the file system from the device left this list in v1.3.0, which needed the block layer to ask the device anything at all. A block device layer left this list in v1.2.0, which the storage work ahead needs and
which arrived with the reason to build it: the file system could not tell a disk it had
failed to read from a disk with nothing on it, and formatted the second. And the sticky
bit on `/tmp`, which v0.9.4 added
alongside the other half of the permission enforcement: the file's own mode. v1.0.0 froze
the syscall ABI, which had been waiting on the disk format rather than on `mount` — a
freeze stops numbers from changing meaning and says nothing about adding new ones, so
`mount` and `umount` take 71 and 72 whenever they are written. That sentence said "68 and
69" until v1.4.0, having been written before `SETKEY` took 68 and left standing for three
releases four lines below the paragraph that says `SETKEY` took 68; it said "70 and 71"
until `USBINFO` took 70 in v1.8.0. It will keep moving, which is why the number that
matters is the one `tests/kernel/test_abi.c` asserts rather than the one written here.

UEFI boot left this list in v1.7.0, and it left for one line of configuration.
The row said the release would need Multiboot2; it needed nothing of the sort.
GRUB's Multiboot 1 command loads a 32-bit kernel perfectly well from 64-bit EFI
firmware — what it could not do was set a video mode, because on a BIOS machine
`grub-mkrescue` builds the video driver into the core image and on UEFI it is a
separate module. The failure said `no suitable video mode found`, which reads
like a complaint about the resolution and is GRUB saying it has no video driver
loaded. `insmod all_video` in `grub.cfg` is the whole of the fix, and no line of
the kernel changed.

What the release did carry is a build dependency nobody had noticed was missing.
`grub-mkrescue` succeeds without `grub-efi-amd64-bin` and quietly produces an
image with no EFI boot path at all — so every ISO this project published before
v1.7.0 booted on BIOS only, and anyone with a UEFI-only machine could not start
it.

The screen left this list in v1.6.0, and it left cheaper than the row had
promised. The terminal is eight hundred lines of scrollback, escape sequences and
cursors, and exactly six of them stored a word at `0xB8000` or wrote a CRT
controller register — so the work was a seam of two calls rather than a rewrite.
The font is ninety-five glyphs written in this repository, because the bitmap
fonts kernels usually embed are ROM dumps with no licence attached. Multiboot2
went with the boot work where it belongs: Multiboot 1 has a video request of its
own, four fields in the header, and it is what asks the bootloader for pixels
here.

A SATA driver left this list in v1.5.0, which is the release the three before it
had been building towards without saying so: v1.2.0 put a block device layer under
the file system and wrote in its own header that the registry would grow when a
second device arrived, v1.3.0 made the file system take its geometry from whatever
device it was on, and v1.4.0 gave the kernel a way to find a controller. On a
modern board the kernel could name the controller holding its disk and not drive
it. Now it drives it, and nothing under `fs/` knows which of the two it is on.

PCI bus enumeration left this list in v1.4.0, and it left carrying more than the row
promised. The row called it the thing neither AHCI nor USB can be written without, which
is true and was not the reason to do it first: `ata_identify()` waited for the busy bit
with no timeout, and a bus with no controller on it answers every read with all ones,
which has that bit set. On the hardware this project is aiming at — a modern board, where
there is no IDE controller at 0x1F0 — the kernel did not fail to find a disk, it stopped,
before the file system and before anything reached the screen. The wait is bounded now,
and the enumeration is what lets the machine say what it has instead of being assumed.

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
