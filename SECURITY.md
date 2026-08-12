# Security Policy

## Supported Versions

| Version | Supported          |
| ------- | ------------------ |
| 0.2.x   | :white_check_mark: |
| 0.1.x   | :x:                |

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
- **Uniqueness holds within a boot, not across boots.** Two cold boots of the same image
  inside the same RTC second are not provably distinct. A persisted seed would close
  this and is on the roadmap.

### Memory and process protection

- No ASLR (Address Space Layout Randomization).
- No stack canaries in user-space programs. The kernel has stack protection.
- No W^X for user pages.
- Single address-space-per-process isolation only; no capability or MAC system.

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
- Persisting an entropy seed across boots
- Adding stack canaries to user-space programs
- Implementing ASLR
- W^X enforcement for user pages

Already done, so no longer needed: per-user password salting (PBKDF2-HMAC-SHA256 with a
16-byte salt) and the entropy pool (interrupt-jitter sourced, with per-source budgets and
an honest quality verdict).
