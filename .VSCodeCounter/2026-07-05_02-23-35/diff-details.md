# Diff Details

Date : 2026-07-05 02:23:35

Directory /Users/esadfurkanduman/kernel__

Total : 61 files,  1392 codes, -109 comments, 308 blanks, all 1591 lines

[Summary](results.md) / [Details](details.md) / [Diff Summary](diff.md) / Diff Details

## Files
| filename | language | code | comment | blank | total |
| :--- | :--- | ---: | ---: | ---: | ---: |
| [Makefile](/Makefile) | Makefile | 63 | 0 | 9 | 72 |
| [apps/bin/hello.c](/apps/bin/hello.c) | C | 13 | 4 | 5 | 22 |
| [apps/hello.asm](/apps/hello.asm) | Assembly | -11 | 0 | -3 | -14 |
| [apps/init.c](/apps/init.c) | C | 176 | -34 | 35 | 177 |
| [arch/x86/boot/boot.asm](/arch/x86/boot/boot.asm) | Assembly | 0 | -6 | -1 | -7 |
| [arch/x86/cpu/isr.c](/arch/x86/cpu/isr.c) | C | 0 | -6 | -5 | -11 |
| [arch/x86/cpu/user\_mode.asm](/arch/x86/cpu/user_mode.asm) | Assembly | 0 | -3 | 0 | -3 |
| [crypto/aes.c](/crypto/aes.c) | C | 1 | 0 | -1 | 0 |
| [drivers/ata.c](/drivers/ata.c) | C | 10 | -13 | 2 | -1 |
| [fs/crypto\_fs.c](/fs/crypto_fs.c) | C | 0 | -37 | -2 | -39 |
| [fs/devfs.c](/fs/devfs.c) | C | 23 | 2 | 5 | 30 |
| [fs/vfs.c](/fs/vfs.c) | C | 9 | -12 | 1 | -2 |
| [include/devfs.h](/include/devfs.h) | C++ | 13 | 0 | 5 | 18 |
| [include/fs.h](/include/fs.h) | C++ | 1 | 0 | 0 | 1 |
| [include/kernel.h](/include/kernel.h) | C++ | 3 | 0 | 1 | 4 |
| [include/libft.h](/include/libft.h) | C++ | 1 | 0 | 0 | 1 |
| [include/pipe.h](/include/pipe.h) | C++ | 17 | 0 | 5 | 22 |
| [include/process.h](/include/process.h) | C++ | 13 | 0 | 3 | 16 |
| [include/stdio.h](/include/stdio.h) | C++ | 3 | 0 | 1 | 4 |
| [include/syscall.h](/include/syscall.h) | C++ | 5 | -1 | 0 | 4 |
| [include/tss.h](/include/tss.h) | C++ | 0 | -1 | 0 | -1 |
| [include/types.h](/include/types.h) | C++ | 0 | 0 | 1 | 1 |
| [kernel/core/kernel.c](/kernel/core/kernel.c) | C | 176 | 1 | 36 | 213 |
| [kernel/core/klog.c](/kernel/core/klog.c) | C | 61 | 0 | 21 | 82 |
| [kernel/elf.c](/kernel/elf.c) | C | -108 | -30 | -32 | -170 |
| [kernel/kernel.c](/kernel/kernel.c) | C | -107 | -14 | -25 | -146 |
| [kernel/proc/elf.c](/kernel/proc/elf.c) | C | 142 | 1 | 30 | 173 |
| [kernel/proc/pipe.c](/kernel/proc/pipe.c) | C | 60 | 0 | 13 | 73 |
| [kernel/proc/process.c](/kernel/proc/process.c) | C | 330 | 0 | 73 | 403 |
| [kernel/proc/signal.c](/kernel/proc/signal.c) | C | 40 | 0 | 7 | 47 |
| [kernel/process.c](/kernel/process.c) | C | -330 | -40 | -82 | -452 |
| [kernel/security.c](/kernel/security.c) | C | -89 | -23 | -20 | -132 |
| [kernel/security/passwd.c](/kernel/security/passwd.c) | C | 0 | 0 | 1 | 1 |
| [kernel/security/security.c](/kernel/security/security.c) | C | 89 | 0 | 15 | 104 |
| [kernel/shell.c](/kernel/shell.c) | C | -168 | 0 | -8 | -176 |
| [kernel/signal.c](/kernel/signal.c) | C | -40 | 0 | -7 | -47 |
| [kernel/syscall.c](/kernel/syscall.c) | C | -394 | -35 | -38 | -467 |
| [kernel/syscall/syscall.c](/kernel/syscall/syscall.c) | C | 737 | 12 | 91 | 840 |
| [lib/ft\_strcmp.c](/lib/ft_strcmp.c) | C | 1 | 0 | 0 | 1 |
| [lib/ft\_strstr.c](/lib/ft_strstr.c) | C | 19 | 0 | 2 | 21 |
| [lib/stdio.c](/lib/stdio.c) | C | 123 | 11 | 27 | 161 |
| [mm/kheap.c](/mm/kheap.c) | C | 0 | -3 | 0 | -3 |
| [mm/pmm.c](/mm/pmm.c) | C | 0 | -18 | -1 | -19 |
| [tests/host/c/fuzz\_parser.c](/tests/host/c/fuzz_parser.c) | C | 38 | 24 | 11 | 73 |
| [tests/host/c/test\_crypto.c](/tests/host/c/test_crypto.c) | C | 55 | 10 | 11 | 76 |
| [tests/host/c/test\_hash.c](/tests/host/c/test_hash.c) | C | 25 | 5 | 7 | 37 |
| [tests/host/python/test\_mkfs.py](/tests/host/python/test_mkfs.py) | Python | 17 | 3 | 8 | 28 |
| [tests/kernel/ktest.h](/tests/kernel/ktest.h) | C++ | 35 | 1 | 7 | 43 |
| [tests/kernel/selftest.c](/tests/kernel/selftest.c) | C | 45 | 3 | 10 | 58 |
| [tests/kernel/test\_adversarial.c](/tests/kernel/test_adversarial.c) | C | 23 | 14 | 10 | 47 |
| [tests/kernel/test\_concurrency.c](/tests/kernel/test_concurrency.c) | C | 15 | 9 | 6 | 30 |
| [tests/kernel/test\_integration.c](/tests/kernel/test_integration.c) | C | 46 | 16 | 14 | 76 |
| [tests/kernel/test\_memory.c](/tests/kernel/test_memory.c) | C | 17 | 0 | 6 | 23 |
| [tests/kernel/test\_pipe.c](/tests/kernel/test_pipe.c) | C | 39 | 15 | 14 | 68 |
| [tests/kernel/test\_regression.c](/tests/kernel/test_regression.c) | C | 40 | 33 | 10 | 83 |
| [tests/kernel/test\_security.c](/tests/kernel/test_security.c) | C | 38 | 12 | 13 | 63 |
| [tests/kernel/test\_stress.c](/tests/kernel/test_stress.c) | C | 27 | 10 | 7 | 44 |
| [tests/kernel/test\_string.c](/tests/kernel/test_string.c) | C | 15 | 0 | 5 | 20 |
| [tests/kernel/test\_vfs.c](/tests/kernel/test_vfs.c) | C | 34 | 1 | 11 | 46 |
| [tools/encrypt\_tool.c](/tools/encrypt_tool.c) | C | 0 | -21 | -6 | -27 |
| [tools/mkfs.py](/tools/mkfs.py) | Python | 1 | 1 | 0 | 2 |

[Summary](results.md) / [Details](details.md) / [Diff Summary](diff.md) / Diff Details