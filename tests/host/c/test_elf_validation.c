/*
 * File: test_elf_validation.c
 * Purpose: Host-side regression tests for the production ELF validator.
 */
#include "types.h"
#include "elf.h"
#include "errno.h"

/*
 * printf is borrowed from the host libc, declared by hand rather than by
 * including a system header. This project ships no third-party library and the
 * kernel builds -nostdlib; these host tests run on Linux purely to print a
 * result, so exactly one symbol is taken and nothing else.
 */
extern int printf(const char *format, ...);

#define ASSERT(condition, message) \
    do { \
        if (!(condition)) { \
            printf("[FAIL] %s\n", message); \
            return 1; \
        } \
    } while (0)

static void zero_bytes(uint8_t *buffer, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) buffer[i] = 0;
}

static void build_valid_elf(uint8_t *image, uint32_t image_len) {
    zero_bytes(image, image_len);

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
    phdr->p_vaddr = 0x00400000;
    phdr->p_filesz = 4;
    phdr->p_memsz = 4;
    phdr->p_flags = 5;
}

int main(void) {
    uint8_t image[sizeof(elf32_ehdr_t) + sizeof(elf32_phdr_t)];
    build_valid_elf(image, sizeof(image));
    ASSERT(elf_validate_image(image, sizeof(image)) == E_OK,
           "valid i386 ELF was rejected");

    elf32_ehdr_t *ehdr = (elf32_ehdr_t *)image;
    elf32_phdr_t *phdr = (elf32_phdr_t *)(image + ehdr->e_phoff);

    phdr->p_filesz = phdr->p_memsz + 1;
    ASSERT(elf_validate_image(image, sizeof(image)) == E_NOEXEC,
           "p_filesz > p_memsz was accepted");

    build_valid_elf(image, sizeof(image));
    ehdr = (elf32_ehdr_t *)image;
    ehdr->e_phoff = 0xFFFFFFF0;
    ASSERT(elf_validate_image(image, sizeof(image)) == E_NOEXEC,
           "overflowing program-header offset was accepted");

    build_valid_elf(image, sizeof(image));
    ehdr = (elf32_ehdr_t *)image;
    phdr = (elf32_phdr_t *)(image + ehdr->e_phoff);
    phdr->p_vaddr = 0xC0000000;
    ASSERT(elf_validate_image(image, sizeof(image)) == E_NOEXEC,
           "kernel-space segment was accepted");

    build_valid_elf(image, sizeof(image));
    ehdr = (elf32_ehdr_t *)image;
    phdr = (elf32_phdr_t *)(image + ehdr->e_phoff);
    phdr->p_flags = 4;
    ASSERT(elf_validate_image(image, sizeof(image)) == E_NOEXEC,
           "non-executable entry point was accepted");

    printf("[PASS] ELF validation rejects malformed images.\n");
    return 0;
}
