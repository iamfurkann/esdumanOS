#ifndef ELF_H
#define ELF_H

#include "types.h"

/**
 * @brief Magic number identifying an Executable and Linkable Format (ELF) file.
 * The value 0x464C457F corresponds to "\x7FELF" in ASCII.
 */
#define ELF_MAGIC 0x464C457F

/**
 * @brief ELF Header structure.
 * This structure sits at the very beginning of an ELF file and provides critical 
 * information regarding the file's layout, architecture, and entry point.
 */
typedef struct {
    uint8_t  e_ident[16]; /**< Magic number and other identification info. */
    uint16_t e_type;      /**< Object file type (2 = Executable). */
    uint16_t e_machine;   /**< Required architecture (3 = x86 / i386). */
    uint32_t e_version;   /**< Object file version. */
    uint32_t e_entry;     /**< CRITICAL: The virtual address to which the system first transfers control. */
    uint32_t e_phoff;     /**< Program Header Table's offset within the file. */
    uint32_t e_shoff;     /**< Section Header Table's offset within the file. */
    uint32_t e_flags;     /**< Processor-specific flags. */
    uint16_t e_ehsize;    /**< ELF header size in bytes. */
    uint16_t e_phentsize; /**< Size of a single Program Header entry. */
    uint16_t e_phnum;     /**< Number of entries in the Program Header Table. */
    uint16_t e_shentsize; /**< Size of a single Section Header entry. */
    uint16_t e_shnum;     /**< Number of entries in the Section Header Table. */
    uint16_t e_shstrndx;  /**< Section header string table index. */
} __attribute__((packed)) elf32_ehdr_t;

/**
 * @brief ELF Program Header structure.
 * Describes a segment or other information the system needs to prepare the program for execution.
 */
typedef struct {
    uint32_t p_type;      /**< Type of segment (1 = Loadable Segment). */
    uint32_t p_offset;    /**< Offset from the beginning of the file to the segment's first byte. */
    uint32_t p_vaddr;     /**< CRITICAL: Virtual address where the segment resides in memory. */
    uint32_t p_paddr;     /**< Physical address (usually ignored on systems with virtual memory). */
    uint32_t p_filesz;    /**< Size in bytes of the segment in the file image. */
    uint32_t p_memsz;     /**< Size in bytes of the segment in memory (often equals p_filesz, larger for BSS). */
    uint32_t p_flags;     /**< Segment permission flags (Read, Write, Execute). */
    uint32_t p_align;     /**< Segment alignment constraint. */
} __attribute__((packed)) elf32_phdr_t;

/**
 * @brief Loads and executes an ELF binary from the filesystem.
 * Parses the ELF header, allocates necessary memory, copies loadable segments,
 * and transfers execution control to the ELF entry point.
 * 
 * @param filename Name of the ELF file to execute.
 * @param parent_id The entry ID of the parent directory where the file resides.
 * @return Usually does not return on success; returns a negative error code on failure.
 */
int load_and_exec_elf(const char *filename, uint8_t parent_id);

#endif