# Security Policy

## Supported Versions

| Version | Supported          |
| ------- | ------------------ |
| 0.9.x   | :white_check_mark: |
| 0.8.x   | :x:                |
| ≤ 0.7.x | :x:                |

Within 0.9.x, use **0.9.1 or later**. 0.9.0 stores permission bits and enforces none of
them, and it creates `/etc/shadow` with the default `0644`; 0.9.1 both enforces the bits
and corrects that file's mode on every boot, including on a disk written by 0.9.0.

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
  mode back if it drifts — the same treatment `/etc/shadow` gets. What that bit is worth
  is bounded by how this system decides access: `check_vfs_access()` is asked about the
  **directory** an operation happens in, not about the file, so a mode on a file is
  recorded and reported rather than standing between a reader and its contents. `/var`
  is `0755`. The seed is not a secret this system can currently keep, and it is written
  down here that way rather than assumed.

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

- **Permission bits decide access as of 0.9.1.** Every entry carries a mode, an owner
  and a group, and `check_vfs_access()` reads them: search permission on every directory
  above the entry, then read or write on the directory the operation happens in, with
  the matching class — owner, group, other — deciding on its own and no fallthrough to
  the next. Until 0.9.1 this compared names, so a file's permissions were a property of
  what it was called and renaming one changed who could touch it.
- **A read asks for read *and* search on the directory.** Unix allows a `0711` directory
  to be searched without being listed; this check is not told whether its caller is
  about to list or to look up a known name, so it asks for both. Stricter than Unix,
  never looser.
- **There is no sticky bit.** `/tmp` is `0777`, so a user can delete another user's file
  there. Restricting deletion to the owner is a separate mechanism this does not have.
- **There is no group database.** A task's group id follows its user id, so a file's
  group is always its owner's. The group bits are read and honoured, but nothing can put
  two users in one group for them to mean anything.
- **`chown` is root-only.** Giving a file away is how a user gets out from under a quota
  on a system that has one. This system has no quota to escape, and the restriction is
  easier to keep now than to add back later.
- **The directory table is read from the disk and largely trusted.** 0.9.0 added a
  superblock, so an image this kernel does not recognise is refused rather than read —
  which is what stops it writing its own tables over a foreign disk. Beyond the
  superblock's geometry, though, the entries themselves are not validated: a crafted
  image can still contain a parent chain that loops, which is why the access walk is
  bounded by the table size rather than by reaching the root.
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
- A sticky bit, so `/tmp` being world-writable does not also make it world-deletable
- Deriving the disk key from a boot passphrase instead of a build-time constant
- Adding stack canaries, in the kernel and in user-space programs
- Implementing ASLR
- Validating the directory table on mount beyond the superblock's geometry, so a crafted
  image cannot present a malformed tree in the first place

Already done, so no longer needed: per-user password salting (PBKDF2-HMAC-SHA256 with a
16-byte salt); the entropy pool (interrupt-jitter sourced, with per-source budgets and
an honest quality verdict); enforcing the permission bits, which in 0.9.1 retired the
last of the name comparisons in `check_vfs_access()`; and persisting an entropy seed
across boots, which 0.9.3 added — for distinctness between boots, not for quality.
