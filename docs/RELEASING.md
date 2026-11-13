# Preparing a Release

This procedure does not imply that the current tree has already been committed,
tagged or published. Use the exact source revision and binary hashes recorded
for the release being prepared.

## Source and Documentation

- Reconcile `include/kernel.h`, README, CHANGELOG and SECURITY. Historical
  changelog entries describe their own releases, not current guarantees.
- Document syscall/program compatibility, format changes and upgrades. v1.14
  keeps v3 but needs the [program refresh](../README.md#upgrading-an-existing-v3-root)
  on older roots. Formatting is not an upgrade procedure.
- Exclude build products, populated disks, credentials, private handoff and
  instruction files, and local evidence from source commits.
- Record tested source/firmware configurations. Assess and validate later
  source changes before carrying forward earlier results.

## Build and Validate

Use the x86 Linux environment in [CONTRIBUTING.md](../CONTRIBUTING.md).
Keep `ESDUMAN_ELF_KEY_HEX` stable. Force a production rebuild if the key,
flags or build flavor changes:

```bash
make -B BUILD=prod all
```

**`make run` is not required to obtain the ISO.** It launches an interactive
guest. The final ISO/kernel names are shared with `run-dev`; verify the build
flavor instead of trusting the name. Never publish a reduced-KDF test/dev ISO.

Complete the eight CI commands in CONTRIBUTING and relevant production
BIOS/UEFI terminal/persistence checks. Storage changes also require
`make test_powercut`, retaining every outcome, including refused inconsistent
snapshots. These do not establish physical-disk crash recovery. Keep evidence
outside `build/` when `make clean` will be used.

Verify the production payload and both boot paths:

```bash
esduman_release_check=$(mktemp -d)
xorriso -osirrox on -indev esdumanOS-v1.14.0-beta.1.iso \
    -extract /boot/myos.bin "$esduman_release_check/myos.bin"
cmp myos.bin "$esduman_release_check/myos.bin"
grub-file --is-x86-multiboot myos.bin
grub-file --is-x86-multiboot2 myos.bin
xorriso -indev esdumanOS-v1.14.0-beta.1.iso -report_el_torito plain
```

The catalog must contain BIOS and UEFI entries. Record compiler flags and
inspect symbols for test-only entry points too; these commands alone do not
prove the KDF cost or absence of test code.

## Assets

Attach the production ISO and checksum file. GitHub supplies source archives
for the tag separately. Exclude `disk.img`, `usb.img`, `usb2.img`, test fixtures,
firmware variable files, keys and populated filesystems. The ISO boots the
kernel; it is not a preformatted encrypted data disk.

On Linux:

```bash
sha256sum esdumanOS-v1.14.0-beta.1.iso > SHA256SUMS
sha256sum -c SHA256SUMS
```

After copying both files into one directory on macOS:

```bash
shasum -a 256 -c SHA256SUMS
```

Regenerate checksums after any rebuild. An old ISO's successful boot does not
prove the rebuilt ISO passed that gate; reconcile binary-specific evidence.

## Git and Publication

The maintainer performs Git steps. Prepare separate commits and a PR body
with truthful `[x]` markers. After review and remote CI, merge and update a
clean local `main` using `git pull --ff-only`. Check that the merge includes
the tested changes. If conflict resolution changed source, reassess validation.

For an unsigned annotated prerelease tag, after verifying the commit:

```bash
git status --short
git log -1 --oneline
git tag -a v1.14.0-beta.1 -m "esdumanOS v1.14.0-beta.1"
git show --no-patch v1.14.0-beta.1
git push origin v1.14.0-beta.1
```

Run these only after revision checks pass and the tag is unused. Use `git tag
-s` instead if a configured signing identity is part of the maintainer's
workflow; an annotated tag is not a signature. Do not move a published tag
to substitute new assets.

Create a GitHub **pre-release** from that tag, include the release notes and
attach the ISO and `SHA256SUMS`. State known limits and actual validation
scope. Verify uploaded files/checksums and links after publishing. Updating
another checkout is separate: do not discard its uncommitted work to pull.
