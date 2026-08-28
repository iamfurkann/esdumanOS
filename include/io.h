#ifndef IO_H
#define IO_H

#include "types.h"

/**
 * @brief Writes a single byte (8 bits) to the specified I/O port.
 * 
 * This inline assembly function interacts directly with the x86 hardware 
 * architecture by sending a single byte to a hardware I/O port using the 
 * 'outb' instruction.
 * 
 * @param port The 16-bit I/O port address.
 * @param val The 8-bit value to write to the port.
 */
static inline void outb(uint16_t port, uint8_t val) {
    asm volatile ( "outb %0, %1" : : "a"(val), "Nd"(port) );
}

/**
 * @brief Reads a single byte (8 bits) from the specified I/O port.
 * 
 * This inline assembly function interacts directly with the x86 hardware 
 * architecture by receiving a single byte from a hardware I/O port using 
 * the 'inb' instruction.
 * 
 * @param port The 16-bit I/O port address.
 * @return uint8_t The 8-bit value read from the port.
 */
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile ( "inb %1, %0" : "=a"(ret) : "Nd"(port) );
    return ret;
}

/**
 * @brief Reads a word (16 bits) from the specified I/O port.
 * 
 * This inline assembly function receives a 16-bit value from a hardware 
 * I/O port using the 'inw' instruction.
 * 
 * @param port The 16-bit I/O port address.
 * @return uint16_t The 16-bit value read from the port.
 */
static inline uint16_t inw(uint16_t port) {
    uint16_t ret;
    asm volatile ("inw %1, %0" : "=a" (ret) : "dN" (port));
    return ret;
}

/**
 * @brief Writes a word (16 bits) to the specified I/O port.
 * 
 * This inline assembly function sends a 16-bit value to a hardware 
 * I/O port using the 'outw' instruction.
 * 
 * @param port The 16-bit I/O port address.
 * @param value The 16-bit value to write to the port.
 */
static inline void outw(uint16_t port, uint16_t value) {
    asm volatile ("outw %1, %0" : : "dN" (port), "a" (value));
}

/*
 * The 32-bit pair, added in v1.4.0 because PCI configuration space cannot be
 * reached without it: the address is written to port 0xCF8 and the data read
 * back from 0xCFC, both as doublewords, and a 32-bit register is the only unit
 * either port accepts.
 *
 * They were the last thing missing from this header, and their absence is why
 * PCI enumeration had been the prerequisite of the prerequisite for three
 * releases - the roadmap said so in the same row.
 */

/**
 * @brief Reads a doubleword (32 bits) from the specified I/O port.
 *
 * @param port The 16-bit I/O port address.
 * @return uint32_t The 32-bit value read from the port.
 */
static inline uint32_t inl(uint16_t port) {
    uint32_t ret;
    asm volatile ("inl %1, %0" : "=a" (ret) : "dN" (port));
    return ret;
}

/**
 * @brief Writes a doubleword (32 bits) to the specified I/O port.
 *
 * @param port The 16-bit I/O port address.
 * @param value The 32-bit value to write to the port.
 */
static inline void outl(uint16_t port, uint32_t value) {
    asm volatile ("outl %1, %0" : : "dN" (port), "a" (value));
}

#endif