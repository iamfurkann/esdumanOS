# Security Policy

## Supported Versions

This tree targets **1.14.0-beta.1**. Fixes are developed against the current
minor line; older minor lines receive no maintenance commitment. The version
in this tree is not evidence that a GitHub release has been published.

| Source line | Maintenance target |
| --- | --- |
| 1.14.x | Current development/release candidate |
| 1.13.x and earlier | Unsupported |

esdumanOS is an educational, experimental operating system, not a production
security boundary and not suitable for protecting sensitive data. Passing
tests do not establish cryptographic assurance or physical-device safety.

Older versions also have concrete defects: before 0.4.2 there were known
memory-safety failures; 0.9.0–0.9.3 exposed password hashes to other accounts;
before 1.1.0 the disk key was compiled into the kernel; before 1.2.0 a failed
read could be mistaken for a blank disk and trigger formatting. QEMU is not
inherently immune to I/O failure; driver fault injection now covers refusals.

There is no converter from pre-v3 layouts. v0.9.x and v0.10.x/1.0.x disks are
refused rather than interpreted as v3. Existing v3 disks are readable, but
their older programs need the [v1.14 refresh](README.md#upgrading-an-existing-v3-root).

## Threat Model

Account permissions and uaccess checks separate ordinary programs from other
users and the kernel. A boot passphrase wraps a volume key; file reads verify
an HMAC over plaintext. Neither mechanism authenticates the kernel/bootloader,
physical hardware or the selected boot disk. There is no verified boot or IOMMU.
An attacker able to replace firmware/kernel code can observe passwords and keys.

Names, hierarchy, ownership, modes, lengths and FAT allocation are plaintext
metadata. Structural validation rejects malformed metadata but is not a keyed
authenticity check. File HMACs do not bind a blob to its pathname and do not
prevent replay of an older valid blob or disk snapshot. `stat`'s reported size
does not authenticate file contents. Entropy quality and power-loss limits
below also bound what disk encryption provides.

## Known Security Limitations

The following are known and documented. They are stated precisely, because a limitation
described inaccurately is worse than one described plainly — in either direction.

### Key management

- **A boot passphrase protects each volume’s generated data key**, as of
  1.1.0. Two levels: PBKDF2-HMAC-SHA256 over the passphrase produces a key-encryption
  key, which unwraps a random data key held in a key slot in the superblock. The data
  key encrypts the file system; the key slot holds the salt, the iteration count, the
  IV, the wrapped key and an HMAC-SHA256 tag over `salt || iv || wrapped_key`. The iteration
  count is range-checked and used in key derivation, not directly included
  in that HMAC input. A wrong passphrase fails tag verification. Three failed
  attempts at the boot prompt stop the machine; secondary attach returns an
  error to its caller instead.
- **There is no fallback to a compiled-in key and no recovery path.** A disk whose key
  slot was zeroed would otherwise decrypt under a key extractable from the binary,
  which would be a downgrade an attacker could force by editing 116 bytes; a disk in
  the older v2 format is refused by its recorded version instead. The other side of
  that is real: there is no recovery for a forgotten passphrase. There is one
  key slot per disk. `setkey` rewraps the root slot only;
  there is no secondary-volume passphrase-change command.
- **The passphrase protects the disk at rest and nothing beyond it.** After boot the
  key is in RAM and any root process reads every file through the mounted file system.
  `LOCKDOWN` wipes the root and secondary data keys; decrypted buffers
  already held by running processes are not thereby erased.
- **A separate build-time key**, `ESDUMAN_ELF_KEY_HEX`, decrypts programs
  embedded in the kernel. Anyone holding the image can recover that key and
  replace/re-encrypt programs; it is packaging, not authenticity against an
  attacker who controls the image. Installed programs are re-encrypted under
  the disk's key. This parameter is neither a login nor a disk password.
- **KDF costs are build-specific.** Production defaults/minimums are 600000
  rounds and the maximum accepted count is 10000000. Out-of-range counts are
  rejected, not clamped. Test/dev costs are lower; those images must not be
  distributed as production or reused as production password databases.

### Entropy

- The pool uses RDRAND when available and interrupt timing otherwise. PIT
  samples receive zero credit, ATA completions have a 64-bit lifetime budget,
  and PS/2 keyboard timing can accumulate credit. USB keyboard input currently
  adds no separate entropy sample. Without RDRAND and sufficient credited
  input, the pool remains `ENTROPY_WEAK`. Its credit is an implementation
  estimate, not an independent proof of entropy.
- Volume data keys, salts and wrapping IVs can be generated while the pool
  reports weak quality. A strong password cannot compensate for a predictable
  underlying data key. This implementation does not block every such operation
  until independently established cryptographic entropy is available.
- File IVs and account salts mix counters/pool output through hashes. Counter
  inputs are distinct before wrap within one boot; truncated hash outputs
  still have collision probability. Earlier documentation incorrectly promised
  unconditional IV/salt uniqueness; retained diagnostic/test labels using
  “unique” do not establish that guarantee either. It also overstated protection gained
  merely by keying a hash when that key itself may have weak entropy.
- `/var/random-seed` mixes 32 bytes of prior state after the root has been
  unlocked, and is credited zero entropy. A replacement is attempted at load
  and at checkpoints. Failed writes, power loss, image cloning and snapshot
  rollback can reuse old state. It is not an anti-rollback mechanism and does
  not strengthen the initial format's earlier key generation retroactively.
- The seed is root-owned `0600`, reapplied at boot. These permissions restrict
  online access; they do not make a weak or replayed seed cryptographically
  strong. The file is encrypted like other ordinary files on the volume.

### Memory and process protection

- No ASLR (Address Space Layout Randomization).
- **No stack canaries anywhere.** This said "none in user-space programs; the kernel has
  stack protection", which was misleading in both halves: nothing is built with
  `-fstack-protector`, kernel or user. What does exist is a guard page mapped below every
  user stack, so a stack that grows past its allocation faults instead of running into
  whatever is beneath it.
- **No W^X for user pages, and it is not expressible here.** 32-bit paging without PAE
  has no per-page execute bit at all, so mapped user pages cannot independently be marked non-executable.
  Read-only ELF text is supported and enforced; writable pages can also execute. This is a property of
  the paging mode rather than something left unimplemented.
- Kernel pages are supervisor-only and `CR0.WP` is enabled, so a write through a
  read-only mapping faults in Ring 0 as well as Ring 3.
- Single address-space-per-process isolation only; no capability or MAC system.

### File access control

- **Each traversed directory requires search permission**, including a component
  followed by `..`. As of 1.14.0, `private/../file` cannot bypass the permission
  on `private`. The checks use the target volume's metadata and are covered by
  unprivileged Ring 3 tests on the second filesystem.
- **Login accepts the same 63-character password limit as account setup.**
  Earlier login screens truncated at 31 characters. Login, disk setup and shell
  password prompts now reject an overlong line instead of using its prefix.
  AUTH preserves trailing spaces, matching the bytes hashed during account
  setup, rather than silently changing the supplied password.
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
- **There is no group database.** A task's group id follows its user id, by default. Root can set a file's stored owner and group independently with
  `chown`; there is no group-membership database or supplementary-group API.
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
- Root (uid 0) bypasses discretionary mode checks. It does not bypass
  immutable-state, invalid-metadata or unavailable-key checks.

### Other

- LOCKDOWN blocks new programs and destroys both mounted in-RAM data keys, after which
  encrypted VFS access is refused rather than attempted with a zeroed key. It does not
  terminate an already-running shell.
- The block cache is write-back. It attempts write-back at 32 dirty slots, on a
  yield-driven 5-second deadline, and on `sync()`. These are scheduling policies,
  not durability bounds when I/O fails or the kernel does not yield. Invalid
  requests are rejected before cache admission; all accepted data stays dirty on
  write failure, regardless of errno. A completely unwritable cache rejects new
  misses with `E_IO` after one attempt per slot. `sync()` reports that failure;
  reboot, halt and mount changes refuse to discard the remaining dirty data.
  Successfully submitted sectors are not a transaction or proof of persistence
  through a device's volatile write cache. Abrupt power loss can still lose data
  or leave filesystem metadata inconsistent. See
  [the reliability audit](docs/engineering/RELIABILITY_AUDIT.md).
- **The v3 filesystem is not journaled.** Sector-boundary power-cut experiments
  during create and delete left orphan allocations. The stricter mount validator
  refuses those images, which can prevent boot even when other files remain
  intact. The offline checker reports this without modifying the image; there
  is no automatic repair. A successful `sync` preserves the tested payload in
  QEMU, but is not a claim about torn sectors or hardware write-cache barriers.
- **Each mounted disk has its own data key and metadata.** A root-only attach
  at `/mnt` validates partition geometry and directory entries, opens the
  secondary key slot with that disk's passphrase, then publishes the candidate.
  A failed attach leaves the root usable. Detach checks live file descriptors
  and working directories, flushes the target and wipes its key. Old directory
  handles expire when that volume is detached.
- **Formatting requires an explicit device.** The `mkfs` syscall checks the
  device name, expected sector count, mounted state and every sector's contents.
  A read failure or any nonzero byte prevents initialization. The first-boot
  `mkfs=<device>` option also requires an exact device name. Ordinary mount and
  fallback discovery only read filesystem metadata. Plaintext metadata and
  device volatile write caches remain outside the encryption guarantee.
- **Boot fallback can select an attached disk as root.** If the initial device
  has no structurally valid filesystem, registered alternatives are probed.
  The selected disk is named on the console/log and must be unlocked, but its
  partition type/signature does not prove who created it. Its `/etc/shadow`
  becomes the account database. Physical root-media substitution is outside
  this kernel's trust guarantees; the format can be produced by another tool.
- **The firmware's own tables are parsed now, and the firmware is not this kernel's
  code.** ACPI arrived in v1.12.0 and with it a walk over structures nothing here wrote:
  an RSDP, a root table, a FADT, and the DSDT's AML. Every length in every one of them
  comes from the firmware, which makes each a walk condition rather than a fact - the
  same discipline the USB descriptor walk carries below. Both RSDP checksums are verified
  rather than only the first, because the second covers the half holding the XSDT address;
  every table's checksum is verified before a field is read from it; a declared length
  below the header or past the copy the kernel keeps is refused rather than summed; and
  the `\_S5_` search will not act on a match that is not introduced by a NameOp, because
  the four characters occur inside ordinary strings and acting on one would send an
  arbitrary value to a hardware register. A table above 4 GB is skipped rather than
  truncated. This is a smaller boundary than a mounted disk - firmware that wanted to
  subvert this kernel does not need a malformed table to do it - but it is a new one, and
  the bound on it is that only S5 is ever entered and only through ports the FADT named.
- **HID report bytes are untrusted indices.** The translation table has
  232 entries, while a byte represents 256 values. The documentation review
  found an unchecked read for usages 0xE8–0xFF; v1.14 now rejects those values
  before indexing. Regressions cover all 24 values, their press/release reports
  and valid input afterwards. This does not authenticate a keyboard: a device
  can still inject ordinary supported keystrokes.
- **USB descriptors are another untrusted structure this kernel walks.** Every
  length in a configuration descriptor comes from the device, and a USB device is
  something a person plugs in. The walk refuses a zero length rather than stepping over
  it, stops at the buffer it was given, and clamps `wTotalLength` to one frame instead of
  believing it; the device and product identifiers it records are reported and never
  used to decide anything. What it does not do is authenticate the device, and this driver implements no device-authentication protocol. Parser bounds
  do not make a hostile device trustworthy.
- Two drivers now enable PCI bus mastering and hand a controller physical addresses to
  read and write: AHCI since 1.5.0, and XHCI since 1.8.0. A bus-mastering device is not
  constrained by the page tables — there is no IOMMU here and no plan for one — so a
  controller that misbehaves, or firmware that is still driving one, can reach any
  physical memory. This is inherent to doing DMA on this platform and is recorded here
  because it was not recorded when it first became true. The XHCI driver asks the
  firmware to release the controller before programming it, which is the part of the
  problem that is addressable.

### Corrected in 0.2.0

Earlier releases documented four limitations that were either no longer true or never
true. For anyone who read them and drew conclusions:

- A default root password of `1234` — **there is none, and never was in the shipped
  tree.** First boot prompts for passwords; only self-test builds auto-create accounts,
  with the password `test`.
- "Password hashing does not use per-user salt" — **it does**, a 16-byte per-user salt.
- "Key derivation uses a custom KDF, not a standard algorithm" — **it is
  PBKDF2-HMAC-SHA256**, with SHA-256 known-answer tests in the repository.
  The former RFC 6070 attribution was incorrect for those SHA-256 tests.
- "Boot-time encryption key may be visible in the GRUB configuration" — **it is not**;
  see Key management above for where it actually lives.

## Reporting a Vulnerability

If you discover a security vulnerability in esdumanOS, please report it responsibly:

1. **Do NOT** open a public GitHub issue for security vulnerabilities.
2. Use [GitHub's private vulnerability reporting](https://github.com/iamfurkann/esdumanOS/security/advisories/new).
3. Include a description of the vulnerability, steps to reproduce, and potential impact.
4. You will receive an acknowledgment within 48 hours.

## Security Updates

Security fixes will be prioritized and released as patch versions when applicable.

## Scope

Please note that as a hobby/educational OS kernel, the security model is intentionally simplified. Reports about the known limitations listed above will be acknowledged but may not result in immediate changes.

We welcome contributions that improve the security posture of esdumanOS. Currently open:
- Adding stack canaries, in the kernel and in user-space programs
- Implementing ASLR

Already done, so no longer needed: deriving the disk key from a boot passphrase, which
1.1.0 added along with re-encrypting the embedded `/bin` images under it; per-user
password salting (PBKDF2-HMAC-SHA256 with a
16-byte salt); the entropy pool (interrupt-jitter sourced, with per-source budgets and
an honest quality verdict); validating the directory table on mount, which 0.10.0 added
so that a crafted image cannot present a tree with a cycle in it or an entry whose id is
not its own slot; enforcing the permission bits, which took two releases —
0.9.1 for the directories and 0.9.4 for the files themselves, the latter also retiring
three name comparisons still hiding in the VFS below the syscall layer; a sticky bit on
`/tmp`, which 0.9.4 added along with them; and persisting an entropy seed across boots,
which 0.9.3 added — to mix previous state into later boots, with no extra entropy credit or
anti-rollback guarantee.
