# Changelog

## [0.1.0-alpha] - 2026-07-26

### Fixed
- Fixed: `kfree` Triple Fault crash during process exit by deferring active stack unmapping in `schedule()`.
- Fixed: Kernel memory leak in `exit_current_process` by properly reclaiming zombie task memory.
- Fixed: Boot hangs caused by debug paging identity maps.

### Changed
- Changed: Modularized `kernel_main` into smaller init subsystems (boot, memory, fs, userspace).
- Changed: Removed obsolete `process_new.c` implementation.

### Security
- Security: Replaced hardcoded "1234" AES IV with dynamic `/dev/urandom` 16-byte generation in `encrypt_tool` and `vfs.c`.
- Security: Removed plaintext `passwords.txt` from repository.
