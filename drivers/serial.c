/*
 * File: serial.c
 * Purpose: Serial port driver implementation.
 *
 * This file is part of the esdumanOS test suite.
 */
#include "serial.h"

/**
 * @brief Initializes the COM1 serial port.
 */
void init_serial(void) {
    outb(PORT_COM1 + 1, 0x00);    // Disable all interrupts
    outb(PORT_COM1 + 3, 0x80);    // Enable DLAB (Divisor Latch Access Bit) for baud rate setting
    outb(PORT_COM1 + 0, 0x01);    // Baud rate divisor Low Byte (115200 / 1 = 115200 baud)
    outb(PORT_COM1 + 1, 0x00);    // Baud rate divisor High Byte
    outb(PORT_COM1 + 3, 0x03);    // 8 bits, no parity, 1 stop bit (8N1)
    outb(PORT_COM1 + 2, 0xC7);    // Enable FIFO, clear them, with 14-byte threshold
    outb(PORT_COM1 + 4, 0x0B);    // Enable IRQs, set RTS/DSR
    outb(PORT_COM1 + 1, 0x01);    // Re-enable interrupts (if using interrupt-based reading)
}

/**
 * @brief Checks if the serial transmit buffer is empty.
 * @return Non-zero if empty, 0 otherwise.
 */
static int is_transmit_empty(void) {
    return inb(PORT_COM1 + 5) & 0x20;
}

/**
 * @brief Writes a single character to the serial port.
 * @param c The character to write.
 */
void serial_write_char(char c) {
    int timeout = 100000;
    
    while (is_transmit_empty() == 0) {
        timeout--;
        if (timeout == 0) {
            return; 
        }
    }
    
    outb(PORT_COM1, c);
}

/**
 * @brief Prints a null-terminated string to the serial port.
 * @param str The string to print.
 */
void serial_print(const char *str) {
    for (int i = 0; str[i] != '\0'; i++) {
        serial_write_char(str[i]);
    }
}