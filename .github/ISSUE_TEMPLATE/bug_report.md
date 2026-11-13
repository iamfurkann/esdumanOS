---
name: Bug Report
about: Report a bug or unexpected behavior in esdumanOS
title: '[BUG] '
labels: bug
assignees: ''
---

## Bug Description

A clear and concise description of what the bug is.

For vulnerabilities, use the private reporting route in
[SECURITY.md](https://github.com/iamfurkann/esdumanOS/blob/main/SECURITY.md).

## Steps to Reproduce

1. Boot esdumanOS in QEMU with '...'
2. Select the disk/firmware and log in (do not include the password)
3. Run command '...'
4. See error

## Expected Behavior

A clear description of what you expected to happen.

## Actual Behavior

What actually happened, including any error messages or kernel panics.

## Environment

- **Build host OS/architecture:** (include VM/emulation details)
- **QEMU version and full invocation:**
- **Firmware/machine/CPU:** (BIOS or OVMF, i440fx or q35, CPU flags)
- **esdumanOS version and source commit:** (e.g., v1.14.0-beta.1; list local changes)
- **Build flavor and command:** (`prod`, `test` or `dev`; omit build keys)
- **ISO SHA-256:** (if booting an ISO)
- **Storage topology:** (device names, sector counts, controller/USB ports)
- **Disk history:** (new image or existing v3; old program refresh performed?)
- **Test selection/verdict:** (MODULE, timeout, final result or last observed stage)

## Serial/Kernel Log Output

```
Paste any relevant serial output or dmesg output here
```

Include the error and nearby lines. Redact passwords and private keys. Do not
attach populated disk images. Report a timeout without a verdict as a timeout.

## Additional Context

Add any other context about the problem here.
