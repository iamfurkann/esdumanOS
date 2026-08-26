# Security Policy

## Supported Versions

| Version | Supported          |
| ------- | ------------------ |
| 0.9.x   | :white_check_mark: |
| 0.8.x   | :x:                |
| ≤ 0.7.x | :x:                |

Use **0.9.4 or later**, and this is not a preference. On every release from 0.9.0 to
0.9.3 inclusive, `/etc/shadow` can be read by any account on the system — 0.9.0 because
it enforced no permission bits at all and created the file `0644`, and 0.9.1 through
0.9.3 because they enforced the bits on directories only and never on the file. 0.9.4
asks the file's own mode and closes it. The hashes are salted PBKDF2-HMAC-SHA256
throughout, so what was exposed is hashes rather than passwords.

0.10.0 changed the on-disk format again and **will not read a disk written by 0.9.x**.
It recognises one and says so rather than reading it as though the layout had not moved.

Only the current minor line is supported. This table said 0.4.x for five minor
releases; it is the current line that matters, and it is 0.9.x.

Releases before 0.4.2 carry known memory-safety defects fixed in that patch and should
not be used at all. Everything between 0.4.2 and 0.8.4 works but is unsupported, and
0.9.0 will not read a disk written by any of them — see below.

## ⚠️ Important Notice

esdumanOS is an **Alpha** educational/experimental operating system. It is **not designed for production use** and should not be used to protect sensitive data. The security features implemented are for learning and demonstration purposes.

## Known Security Limitations

The following are known and documented. They are stated precisely, because a limitation
described inaccurately is worse than one described plainly — in either direction.

### Key management

- **The disk encryption key is a build-time constant compiled into the kernel image.**
  It is set from `ESDUMAN_ELF_KEY_HEX` at build time and embedded in the binary. This
  provides *tamper resistance* — a modified `/bin` program will not decrypt — and
  **not** confidentiality at rest: anyone holding the kernel image holds the key. The
  kernel states this in its own boot log. It is not exposed in the GRUB configuration.
- Deriving the key from a boot passphrase instead is on the roadmap.

### Entropy

- **Without RDRAND, the entropy pool does not reach cryptographic quality, and reports
  that honestly** rather than claiming otherwise. It is fed by interrupt timing; the PIT
  is periodic and is credited nothing, ATA completions are capped at 64 bits for the
  entire boot, and the keyboard is the only source whose credit scales. On a headless
  machine the pool therefore stays at `ENTROPY_WEAK`.
- What *is* guaranteed regardless of entropy quality is **uniqueness**: CryptoFS IVs and
  `/etc/shadow` salts are derived through a monotonic counter, so no two can collide
  within a boot even if every source were worthless. Unpredictability is what degrades,
  not uniqueness.
- **Across boots, uniqueness rests on a seed carried on disk.** `/var/random-seed` holds
  32 bytes, mixed into the pool at boot and replaced immediately, so a machine that
  loses power before its next checkpoint cannot reuse one. Before v0.9.3 this rested on
  the RTC second and the TSC alone, and two cold boots of one image inside the same
  second were not provably distinct.
- **The seed is credited zero entropy, and this is not a technicality.** Bytes written
  by a pool that never reached cryptographic quality do not acquire any by being stored
  and read back, so `entropy_quality()` returns exactly what it would have without one.
  The seed buys distinctness between boots. It does not turn `ENTROPY_WEAK` into
  `ENTROPY_OK` and is not treated as if it could.
- The seed file is created `0600` and owned by root, and the boot-time pass puts that
  mode back if it drifts — the same treatment `/etc/shadow` gets. As of 0.9.4 that bit is
  worth something: `check_file_access()` asks it before an open succeeds. On 0.9.3 it was
  not, because nothing consulted a file's own mode — see **File access control** below.

### Memory and process protection

- No ASLR (Address Space Layout Randomization).
- **No stack canaries anywhere.** This said "none in user-space programs; the kernel has
  stack protection", which was misleading in both halves: nothing is built with
  `-fstack-protector`, kernel or user. What does exist is a guard page mapped below every
  user stack, so a stack that grows past its allocation faults instead of running into
  whatever is beneath it.
- **No W^X for user pages, and it is not expressible here.** 32-bit paging without PAE
  has no per-page execute bit at all, so a user page is readable, writable and
  executable because the hardware offers no way to say otherwise. This is a property of
  the paging mode rather than something left unimplemented.
- Kernel pages are supervisor-only and `CR0.WP` is enabled, so a write through a
  read-only mapping faults in Ring 0 as well as Ring 3.
- Single address-space-per-process isolation only; no capability or MAC system.

### File access control

- **Permission bits decide access, and both halves are asked as of 0.9.4.**
  `check_vfs_access()` handles the directories: search permission on every directory
  above the entry, then read or write on the directory the operation happens in.
  `check_file_access()` then asks the entry's own bits the same question. The matching
  class — owner, group, other — decides on its own, with no fallthrough to the next.
- **Between 0.9.1 and 0.9.3 a file's own mode was not consulted at all**, and that was a
  regression rather than a gap. 0.9.0 refused reads of `shadow` by a name comparison;
  0.9.1 retired the comparison — correctly, since a file's permissions should not be a
  property of what it is called — and put a directory-level check in its place. The
  result was that `/etc/shadow` carried `0600` inside an `/etc` carrying `0755` and any
  user could read it. **On 0.9.1, 0.9.2 and 0.9.3 the password hashes are readable by
  every account on the system.** They are salted PBKDF2-HMAC-SHA256, so what is exposed
  is hashes rather than passwords. Both published releases carry a banner saying so.
- **The execute bit decides what may be run, as of 0.9.4.** Before that it was decided by
  location: anything in `/bin` for anybody, anything else for root alone. A user can now
  run a program they wrote and `chmod 755`'d, which is the Unix arrangement — the ELF
  validator still runs on every load and a new process gets nothing its parent lacked.
- **The sticky bit is enforced, as of 0.9.4.** `/tmp` is `01777`: writable by everyone,
  and removal or renaming restricted to the owner of the entry, the owner of the
  directory, and root. Write permission on a directory is otherwise permission to remove
  anything in it.
- **A read asks for read *and* search on the directory.** Unix allows a `0711` directory
  to be searched without being listed; this check is not told whether its caller is
  about to list or to look up a known name, so it asks for both. Stricter than Unix,
  never looser.
- **The set-user-id and set-group-id bits are stored and enforced by nothing.** They can
  be set and read back, and no program has ever run as anyone but the user who launched
  it. There is no plan to implement them.
- **There is no group database.** A task's group id follows its user id, so a file's
  group is always its owner's. The group bits are read and honoured, but nothing can put
  two users in one group for them to mean anything.
- **`chown` is root-only.** Giving a file away is how a user gets out from under a quota
  on a system that has one. This system has no quota to escape, and the restriction is
  easier to keep now than to add back later.
- **The directory table is checked before it is believed, as of 0.10.0.** 0.9.0 added a
  superblock, so an image this kernel does not recognise is refused rather than read.
  Beyond the superblock's geometry, though, the entries themselves went unvalidated
  until 0.10.0: a crafted image could contain a parent chain that loops, or an entry
  whose id was not the slot it occupied — which every lookup in the VFS assumes. The
  mount path now refuses such a table rather than repairing it, because the information
  needed to repair it is exactly the information in doubt. The bounded walks in
  `check_vfs_access()` and `sys_getcwd()` stay where they are: the table is checked once
  at the door and walked a great many times afterwards, and a guard costing a comparison
  is not worth removing on the strength of a check that ran minutes ago.
- Root (uid 0) bypasses these checks entirely, as it does on any Unix.

### Other

- LOCKDOWN blocks new programs and destroys the in-RAM master key, after which
  encrypted VFS access is refused rather than attempted with a zeroed key. It does not
  terminate an already-running shell.
- The block cache is write-back. Dirty sectors are bounded by volume (32 slots) and by
  time (5 seconds), and `sync()` flushes on demand — but an abrupt power loss inside
  that window still loses the sectors in it.

### Corrected in 0.2.0

Earlier releases documented four limitations that were either no longer true or never
true. For anyone who read them and drew conclusions:

- A default root password of `1234` — **there is none, and never was in the shipped
  tree.** First boot prompts for passwords; only self-test builds auto-create accounts,
  with the password `test`.
- "Password hashing does not use per-user salt" — **it does**, a 16-byte per-user salt.
- "Key derivation uses a custom KDF, not a standard algorithm" — **it is
  PBKDF2-HMAC-SHA256**, verified against RFC 6070 vectors.
- "Boot-time encryption key may be visible in the GRUB configuration" — **it is not**;
  see Key management above for where it actually lives.

## Reporting a Vulnerability

If you discover a security vulnerability in esdumanOS, please report it responsibly:

1. **Do NOT** open a public GitHub issue for security vulnerabilities.
2. Instead, please email the maintainer or use [GitHub's private vulnerability reporting](https://github.com/iamfurkann/esdumanOS/security/advisories/new).
3. Include a description of the vulnerability, steps to reproduce, and potential impact.
4. You will receive an acknowledgment within 48 hours.

## Security Updates

Security fixes will be prioritized and released as patch versions when applicable.

## Scope

Please note that as a hobby/educational OS kernel, the security model is intentionally simplified. Reports about the known limitations listed above will be acknowledged but may not result in immediate changes.

We welcome contributions that improve the security posture of esdumanOS. Currently open:
- Deriving the disk key from a boot passphrase instead of a build-time constant
- Adding stack canaries, in the kernel and in user-space programs
- Implementing ASLR

Already done, so no longer needed: per-user password salting (PBKDF2-HMAC-SHA256 with a
16-byte salt); the entropy pool (interrupt-jitter sourced, with per-source budgets and
an honest quality verdict); validating the directory table on mount, which 0.10.0 added
so that a crafted image cannot present a tree with a cycle in it or an entry whose id is
not its own slot; enforcing the permission bits, which took two releases —
0.9.1 for the directories and 0.9.4 for the files themselves, the latter also retiring
three name comparisons still hiding in the VFS below the syscall layer; a sticky bit on
`/tmp`, which 0.9.4 added along with them; and persisting an entropy seed across boots,
which 0.9.3 added — for distinctness between boots, not for quality.
