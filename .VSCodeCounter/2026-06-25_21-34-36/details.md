# Details

Date : 2026-06-25 21:34:36

Directory /Users/esadfurkanduman/kernel__

Total : 100 files,  5279 codes, 498 comments, 827 blanks, all 6604 lines

[Summary](results.md) / Details / [Diff Summary](diff.md) / [Diff Details](diff-details.md)

## Files
| filename | language | code | comment | blank | total |
| :--- | :--- | ---: | ---: | ---: | ---: |
| [Makefile](/Makefile) | Makefile | 71 | 2 | 16 | 89 |
| [apps/hello.asm](/apps/hello.asm) | Assembly | 11 | 0 | 3 | 14 |
| [apps/init.c](/apps/init.c) | C | 229 | 14 | 26 | 269 |
| [arch/x86/boot/boot.asm](/arch/x86/boot/boot.asm) | Assembly | 27 | 7 | 8 | 42 |
| [arch/x86/cpu/gdt.c](/arch/x86/cpu/gdt.c) | C | 30 | 0 | 8 | 38 |
| [arch/x86/cpu/gdt\_s.asm](/arch/x86/cpu/gdt_s.asm) | Assembly | 18 | 1 | 6 | 25 |
| [arch/x86/cpu/idt.c](/arch/x86/cpu/idt.c) | C | 79 | 3 | 20 | 102 |
| [arch/x86/cpu/idt\_s.asm](/arch/x86/cpu/idt_s.asm) | Assembly | 93 | 1 | 13 | 107 |
| [arch/x86/cpu/isr.c](/arch/x86/cpu/isr.c) | C | 116 | 0 | 22 | 138 |
| [arch/x86/cpu/timer.c](/arch/x86/cpu/timer.c) | C | 13 | 0 | 4 | 17 |
| [arch/x86/cpu/tss.c](/arch/x86/cpu/tss.c) | C | 18 | 0 | 7 | 25 |
| [arch/x86/cpu/user\_mode.asm](/arch/x86/cpu/user_mode.asm) | Assembly | 19 | 4 | 5 | 28 |
| [drivers/ata.c](/drivers/ata.c) | C | 55 | 0 | 18 | 73 |
| [drivers/keyboard.c](/drivers/keyboard.c) | C | 335 | 7 | 32 | 374 |
| [drivers/rtc.c](/drivers/rtc.c) | C | 86 | 0 | 20 | 106 |
| [drivers/tty.c](/drivers/tty.c) | C | 162 | 2 | 36 | 200 |
| [fs/vfs.c](/fs/vfs.c) | C | 201 | 0 | 42 | 243 |
| [grub/grub.cfg](/grub/grub.cfg) | Properties | 3 | 0 | 0 | 3 |
| [include/ata.h](/include/ata.h) | C++ | 22 | 4 | 6 | 32 |
| [include/elf.h](/include/elf.h) | C++ | 31 | 0 | 5 | 36 |
| [include/fs.h](/include/fs.h) | C++ | 30 | 1 | 7 | 38 |
| [include/gdt.h](/include/gdt.h) | C++ | 20 | 0 | 7 | 27 |
| [include/idt.h](/include/idt.h) | C++ | 20 | 0 | 5 | 25 |
| [include/init\_elf.h](/include/init_elf.h) | C++ | 1,348 | 0 | 1 | 1,349 |
| [include/io.h](/include/io.h) | C++ | 20 | 0 | 6 | 26 |
| [include/isr.h](/include/isr.h) | C++ | 18 | 0 | 6 | 24 |
| [include/kernel.h](/include/kernel.h) | C++ | 28 | 5 | 8 | 41 |
| [include/keyboard.h](/include/keyboard.h) | C++ | 5 | 0 | 3 | 8 |
| [include/kheap.h](/include/kheap.h) | C++ | 8 | 0 | 3 | 11 |
| [include/libft.h](/include/libft.h) | C++ | 56 | 0 | 5 | 61 |
| [include/multiboot.h](/include/multiboot.h) | C++ | 28 | 0 | 5 | 33 |
| [include/paging.h](/include/paging.h) | C++ | 11 | 1 | 4 | 16 |
| [include/pmm.h](/include/pmm.h) | C++ | 14 | 0 | 4 | 18 |
| [include/process.h](/include/process.h) | C++ | 43 | 0 | 12 | 55 |
| [include/rtc.h](/include/rtc.h) | C++ | 7 | 0 | 3 | 10 |
| [include/signal.h](/include/signal.h) | C++ | 15 | 1 | 6 | 22 |
| [include/stack.h](/include/stack.h) | C++ | 5 | 0 | 3 | 8 |
| [include/stdio.h](/include/stdio.h) | C++ | 12 | 0 | 3 | 15 |
| [include/tss.h](/include/tss.h) | C++ | 23 | 1 | 4 | 28 |
| [include/tty.h](/include/tty.h) | C++ | 31 | 0 | 3 | 34 |
| [include/types.h](/include/types.h) | C++ | 11 | 0 | 3 | 14 |
| [kernel/elf.c](/kernel/elf.c) | C | 84 | 0 | 20 | 104 |
| [kernel/kernel.c](/kernel/kernel.c) | C | 104 | 1 | 28 | 133 |
| [kernel/process.c](/kernel/process.c) | C | 259 | 0 | 65 | 324 |
| [kernel/signal.c](/kernel/signal.c) | C | 35 | 0 | 6 | 41 |
| [kernel/syscall.c](/kernel/syscall.c) | C | 214 | 0 | 10 | 224 |
| [lib/Makefile](/lib/Makefile) | Makefile | 11 | 0 | 6 | 17 |
| [lib/ft\_atoi.c](/lib/ft_atoi.c) | C | 29 | 11 | 5 | 45 |
| [lib/ft\_bzero.c](/lib/ft_bzero.c) | C | 10 | 11 | 4 | 25 |
| [lib/ft\_calloc.c](/lib/ft_calloc.c) | C | 10 | 11 | 4 | 25 |
| [lib/ft\_isalnum.c](/lib/ft_isalnum.c) | C | 8 | 11 | 2 | 21 |
| [lib/ft\_isalpha.c](/lib/ft_isalpha.c) | C | 6 | 11 | 2 | 19 |
| [lib/ft\_isascii.c](/lib/ft_isascii.c) | C | 6 | 11 | 2 | 19 |
| [lib/ft\_isdigit.c](/lib/ft_isdigit.c) | C | 6 | 11 | 2 | 19 |
| [lib/ft\_isprint.c](/lib/ft_isprint.c) | C | 6 | 11 | 2 | 19 |
| [lib/ft\_itoa.c](/lib/ft_itoa.c) | C | 38 | 11 | 5 | 54 |
| [lib/ft\_lstadd\_back\_bonus.c](/lib/ft_lstadd_back_bonus.c) | C | 16 | 11 | 4 | 31 |
| [lib/ft\_lstadd\_front\_bonus.c](/lib/ft_lstadd_front_bonus.c) | C | 8 | 11 | 3 | 22 |
| [lib/ft\_lstclear\_bonus.c](/lib/ft_lstclear_bonus.c) | C | 17 | 11 | 4 | 32 |
| [lib/ft\_lstdelone\_bonus.c](/lib/ft_lstdelone_bonus.c) | C | 8 | 11 | 3 | 22 |
| [lib/ft\_lstlast\_bonus.c](/lib/ft_lstlast_bonus.c) | C | 11 | 11 | 4 | 26 |
| [lib/ft\_lstnew\_bonus.c](/lib/ft_lstnew_bonus.c) | C | 11 | 11 | 4 | 26 |
| [lib/ft\_lstsize\_bonus.c](/lib/ft_lstsize_bonus.c) | C | 12 | 11 | 4 | 27 |
| [lib/ft\_memchr.c](/lib/ft_memchr.c) | C | 14 | 11 | 4 | 29 |
| [lib/ft\_memcmp.c](/lib/ft_memcmp.c) | C | 16 | 11 | 4 | 31 |
| [lib/ft\_memcpy.c](/lib/ft_memcpy.c) | C | 13 | 11 | 4 | 28 |
| [lib/ft\_memmove.c](/lib/ft_memmove.c) | C | 17 | 11 | 4 | 32 |
| [lib/ft\_memset.c](/lib/ft_memset.c) | C | 14 | 11 | 4 | 29 |
| [lib/ft\_putchar\_fd.c](/lib/ft_putchar_fd.c) | C | 5 | 11 | 3 | 19 |
| [lib/ft\_putendl\_fd.c](/lib/ft_putendl_fd.c) | C | 12 | 11 | 3 | 26 |
| [lib/ft\_putnbr\_fd.c](/lib/ft_putnbr_fd.c) | C | 21 | 11 | 4 | 36 |
| [lib/ft\_putstr\_fd.c](/lib/ft_putstr_fd.c) | C | 11 | 11 | 3 | 25 |
| [lib/ft\_split.c](/lib/ft_split.c) | C | 70 | 0 | 10 | 80 |
| [lib/ft\_strchr.c](/lib/ft_strchr.c) | C | 17 | 11 | 4 | 32 |
| [lib/ft\_strcmp.c](/lib/ft_strcmp.c) | C | 12 | 0 | 2 | 14 |
| [lib/ft\_strcpy.c](/lib/ft_strcpy.c) | C | 13 | 0 | 3 | 16 |
| [lib/ft\_strdup.c](/lib/ft_strdup.c) | C | 12 | 11 | 4 | 27 |
| [lib/ft\_striteri.c](/lib/ft_striteri.c) | C | 12 | 11 | 3 | 26 |
| [lib/ft\_strjoin.c](/lib/ft_strjoin.c) | C | 27 | 11 | 6 | 44 |
| [lib/ft\_strlcat.c](/lib/ft_strlcat.c) | C | 26 | 11 | 5 | 42 |
| [lib/ft\_strlcpy.c](/lib/ft_strlcpy.c) | C | 18 | 11 | 4 | 33 |
| [lib/ft\_strlen.c](/lib/ft_strlen.c) | C | 9 | 11 | 4 | 24 |
| [lib/ft\_strmapi.c](/lib/ft_strmapi.c) | C | 21 | 11 | 4 | 36 |
| [lib/ft\_strncmp.c](/lib/ft_strncmp.c) | C | 13 | 11 | 4 | 28 |
| [lib/ft\_strnstr.c](/lib/ft_strnstr.c) | C | 19 | 11 | 4 | 34 |
| [lib/ft\_strrchr.c](/lib/ft_strrchr.c) | C | 15 | 11 | 4 | 30 |
| [lib/ft\_strtrim.c](/lib/ft_strtrim.c) | C | 52 | 11 | 10 | 73 |
| [lib/ft\_substr.c](/lib/ft_substr.c) | C | 29 | 11 | 7 | 47 |
| [lib/ft\_tolower.c](/lib/ft_tolower.c) | C | 6 | 11 | 2 | 19 |
| [lib/ft\_toupper.c](/lib/ft_toupper.c) | C | 6 | 11 | 2 | 19 |
| [lib/stack.c](/lib/stack.c) | C | 31 | 0 | 15 | 46 |
| [lib/stdio.c](/lib/stdio.c) | C | 44 | 1 | 7 | 52 |
| [lib/utils.c](/lib/utils.c) | C | 43 | 0 | 6 | 49 |
| [lib/utils2.c](/lib/utils2.c) | C | 34 | 0 | 6 | 40 |
| [mm/kheap.c](/mm/kheap.c) | C | 111 | 0 | 30 | 141 |
| [mm/paging.c](/mm/paging.c) | C | 73 | 1 | 17 | 91 |
| [mm/paging\_s.asm](/mm/paging_s.asm) | Assembly | 17 | 0 | 2 | 19 |
| [mm/pmm.c](/mm/pmm.c) | C | 98 | 0 | 21 | 119 |
| [tools/inject.py](/tools/inject.py) | Python | 28 | 0 | 11 | 39 |
| [tools/mkfs.py](/tools/mkfs.py) | Python | 19 | 1 | 10 | 30 |

[Summary](results.md) / Details / [Diff Summary](diff.md) / [Diff Details](diff-details.md)