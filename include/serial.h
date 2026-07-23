#ifndef SERIAL_H
#define SERIAL_H

#include "types.h"
#include "io.h"

/**
 * @brief Standard COM1 Port Address.
 * 
 * Defines the base I/O port address for the primary serial communication interface.
 */
#define PORT_COM1 0x3f8

/**
 * @brief Initializes the serial port.
 * 
 * Configures the COM1 serial port with the appropriate baud rate, parity, 
 * and data bits for reliable kernel debugging and communication.
 */
void init_serial(void);

/**
 * @brief Writes a single character to the serial port.
 * 
 * Waits until the serial transmit buffer is empty before sending the 
 * specified character to the COM1 port.
 * 
 * @param c The character to write.
 */
void serial_write_char(char c);

/**
 * @brief Writes a null-terminated string to the serial port.
 * 
 * Iterates through the provided string and sends each character 
 * to the serial port, commonly used for sending debug messages.
 * 
 * @param str The null-terminated string to transmit.
 */
void serial_print(const char *str);

#endif // SERIAL_H