## Description

Brief description of the changes in this PR.

Include compatibility/upgrade steps if installed programs or disk layouts change.

## Type of Change

- [ ] Bug fix (non-breaking change that fixes an issue)
- [ ] New feature (non-breaking change that adds functionality)
- [ ] Documentation update
- [ ] Test improvement
- [ ] Refactoring (no functional changes)

## Subsystem

Which subsystem(s) does this affect?
- [ ] `mm` — Memory management
- [ ] `fs` — File system
- [ ] `proc` — Process management
- [ ] `drivers` — Device drivers
- [ ] `crypto` — Cryptography
- [ ] `security` — Security module
- [ ] `apps` — User-space programs
- [ ] `tests` — Test infrastructure
- [ ] `build` — Build system / CI
- [ ] `docs` — Documentation

## Testing

- [ ] All existing tests pass (`make test && make test_kernel`)
- [ ] New tests added for this change
- [ ] Tested manually in QEMU

## Checklist

- [ ] Code follows the project's style guidelines
- [ ] Self-reviewed the code
- [ ] Comments added for non-obvious logic
- [ ] Documentation updated if needed

## Validation Details

List observed commands, configurations and results. The test checkbox above is
a baseline, not the full CI matrix: see [CONTRIBUTING.md](../CONTRIBUTING.md).
Distinguish automated terminal/QMP acceptance from manual QEMU interaction and
physical-machine testing. Record timeouts or incomplete runs explicitly.
