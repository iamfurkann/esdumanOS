#ifndef INIT_ELF_H
#define INIT_ELF_H

/*
 * uint8_t is used below, so the header that defines it has to be included.
 * This file had no includes at all and compiled only because its one consumer,
 * kernel/core/kernel.c, includes kernel.h on the line above it - swapping those
 * two lines would have broken the build.
 */
#include "types.h"

/**
 * @brief Embedded ELF binary array.
 * Holds the raw bytes of the initial user-space program (init) compiled into the kernel.
 */
extern unsigned char init_elf[];

/**
 * @brief Length of the embedded init ELF binary in bytes.
 */
extern unsigned int init_elf_len;


// --- Added by Refactor Script ---
extern uint8_t sh_elf[];
extern uint8_t hello_elf[];
extern uint8_t clear_elf[];
extern uint8_t echo_elf[];

#endif