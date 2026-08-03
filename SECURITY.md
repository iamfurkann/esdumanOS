# Security Policy

## Supported Versions

| Version | Supported          |
| ------- | ------------------ |
| 0.1.x   | :white_check_mark: |

## ⚠️ Important Notice

esdumanOS is a **Pre-Alpha** educational/experimental operating system. It is **not designed for production use** and should not be used to protect sensitive data. The security features implemented are for learning and demonstration purposes.

## Known Security Limitations

The following security limitations are known and documented:

- Default root password is `1234` — should be changed after first boot
- Password hashing does not use per-user salt
- Key derivation uses a custom KDF, not a standard algorithm (PBKDF2, scrypt, Argon2)
- Boot-time encryption key may be visible in the GRUB configuration
- PRNG for IV generation relies on RDTSC/RTC (not cryptographically strong)
- No ASLR (Address Space Layout Randomization)
- No stack canaries in user-space programs

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

We welcome contributions that improve the security posture of esdumanOS, including:
- Implementing per-user password salting
- Improving entropy sources for cryptographic operations
- Adding stack protection mechanisms
- Implementing ASLR
