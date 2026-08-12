/*
 * File: elf_validate.c
 * Purpose: Pure, bounds-safe validation for 32-bit i386 ELF images.
 */
#include "elf.h"
#include "errno.h"

#define ELFCLASS32 1
#define ELFDATA2LSB 1
#define EV_CURRENT 1
#define ET_EXEC 2
#define EM_386 3
#define PT_LOAD 1
#define PF_X 1
#define USER_IMAGE_MIN 0x00400000
#define USER_IMAGE_MAX 0xC0000000

int elf_validate_image(const uint8_t *image, uint32_t image_len) {
    if (!image || image_len < sizeof(elf32_ehdr_t)) return E_NOEXEC;

    const elf32_ehdr_t *ehdr = (const elf32_ehdr_t *)image;
    if (ehdr->e_ident[0] != 0x7F || ehdr->e_ident[1] != 'E' ||
        ehdr->e_ident[2] != 'L' || ehdr->e_ident[3] != 'F' ||
        ehdr->e_ident[4] != ELFCLASS32 || ehdr->e_ident[5] != ELFDATA2LSB ||
        ehdr->e_ident[6] != EV_CURRENT || ehdr->e_type != ET_EXEC ||
        ehdr->e_machine != EM_386 || ehdr->e_version != EV_CURRENT ||
        ehdr->e_ehsize != sizeof(elf32_ehdr_t) ||
        ehdr->e_phentsize != sizeof(elf32_phdr_t) || ehdr->e_phnum == 0) {
        return E_NOEXEC;
    }

    if (ehdr->e_phoff > image_len ||
        ehdr->e_phnum > (image_len - ehdr->e_phoff) / sizeof(elf32_phdr_t)) {
        return E_NOEXEC;
    }

    int entry_is_executable = 0;
    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        const elf32_phdr_t *phdr = (const elf32_phdr_t *)(
            image + ehdr->e_phoff + ((uint32_t)i * sizeof(elf32_phdr_t)));

        if (phdr->p_type != PT_LOAD) continue;
        if (phdr->p_filesz > phdr->p_memsz || phdr->p_offset > image_len ||
            phdr->p_filesz > image_len - phdr->p_offset) {
            return E_NOEXEC;
        }

        uint32_t segment_end = phdr->p_vaddr + phdr->p_memsz;
        if (phdr->p_memsz == 0 || segment_end < phdr->p_vaddr ||
            phdr->p_vaddr < USER_IMAGE_MIN || segment_end > USER_IMAGE_MAX) {
            return E_NOEXEC;
        }

        if ((phdr->p_flags & PF_X) && ehdr->e_entry >= phdr->p_vaddr &&
            ehdr->e_entry < segment_end) {
            entry_is_executable = 1;
        }
    }

    return entry_is_executable ? E_OK : E_NOEXEC;
}
