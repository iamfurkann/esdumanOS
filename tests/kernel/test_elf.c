/*
 * File: test_elf.c
 * Purpose: Kernel self-tests for malformed ELF image rejection.
 */
#include "ktest.h"
#include "elf.h"
#include "errno.h"
#include "libft.h"

static void build_valid_elf(uint8_t *image, uint32_t image_len) {
    ft_memset(image, 0, image_len);

    elf32_ehdr_t *ehdr = (elf32_ehdr_t *)image;
    ehdr->e_ident[0] = 0x7F;
    ehdr->e_ident[1] = 'E';
    ehdr->e_ident[2] = 'L';
    ehdr->e_ident[3] = 'F';
    ehdr->e_ident[4] = 1;
    ehdr->e_ident[5] = 1;
    ehdr->e_ident[6] = 1;
    ehdr->e_type = 2;
    ehdr->e_machine = 3;
    ehdr->e_version = 1;
    ehdr->e_entry = 0x00400000;
    ehdr->e_phoff = sizeof(elf32_ehdr_t);
    ehdr->e_ehsize = sizeof(elf32_ehdr_t);
    ehdr->e_phentsize = sizeof(elf32_phdr_t);
    ehdr->e_phnum = 1;

    elf32_phdr_t *phdr = (elf32_phdr_t *)(image + ehdr->e_phoff);
    phdr->p_type = 1;
    phdr->p_offset = 0;
    phdr->p_vaddr = 0x00400000;
    phdr->p_filesz = 4;
    phdr->p_memsz = 4;
    phdr->p_flags = 5;
}

void run_elf_tests(void) {
    uint8_t image[sizeof(elf32_ehdr_t) + sizeof(elf32_phdr_t)];
    build_valid_elf(image, sizeof(image));

    printk("\n--- ELF Loader Validation Tests ---\n");
    KTEST_ASSERT(elf_validate_image(image, sizeof(image)) == E_OK,
                 "ELF: Valid i386 executable accepted");

    elf32_ehdr_t *ehdr = (elf32_ehdr_t *)image;
    elf32_phdr_t *phdr = (elf32_phdr_t *)(image + ehdr->e_phoff);

    phdr->p_filesz = phdr->p_memsz + 1;
    KTEST_ASSERT(elf_validate_image(image, sizeof(image)) == E_NOEXEC,
                 "ELF: Segment with p_filesz larger than p_memsz rejected");

    build_valid_elf(image, sizeof(image));
    ehdr = (elf32_ehdr_t *)image;
    ehdr->e_phoff = 0xFFFFFFF0;
    KTEST_ASSERT(elf_validate_image(image, sizeof(image)) == E_NOEXEC,
                 "ELF: Overflowing program-header offset rejected");

    build_valid_elf(image, sizeof(image));
    ehdr = (elf32_ehdr_t *)image;
    phdr = (elf32_phdr_t *)(image + ehdr->e_phoff);
    phdr->p_vaddr = 0xC0000000;
    KTEST_ASSERT(elf_validate_image(image, sizeof(image)) == E_NOEXEC,
                 "ELF: Kernel-space load address rejected");

    build_valid_elf(image, sizeof(image));
    ehdr = (elf32_ehdr_t *)image;
    phdr = (elf32_phdr_t *)(image + ehdr->e_phoff);
    phdr->p_flags = 4;
    KTEST_ASSERT(elf_validate_image(image, sizeof(image)) == E_NOEXEC,
                 "ELF: Entry point outside executable segment rejected");
}
