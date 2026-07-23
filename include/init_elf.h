#ifndef INIT_ELF_H
#define INIT_ELF_H

/**
 * @brief Embedded ELF binary array.
 * Holds the raw bytes of the initial user-space program (init) compiled into the kernel.
 */
extern unsigned char init_elf[];

/**
 * @brief Length of the embedded init ELF binary in bytes.
 */
extern unsigned int init_elf_len;

#endif