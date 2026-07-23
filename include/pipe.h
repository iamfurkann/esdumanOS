#ifndef PIPE_H
#define PIPE_H

#include "types.h"
#include "registers.h" 

/**
 * @brief Standard buffer size for a pipe in bytes.
 */
#define PIPE_SIZE 4096

/**
 * @brief Structure representing a pipe for Inter-Process Communication (IPC).
 * Implements a ring buffer.
 */
typedef struct {
    uint8_t buffer[PIPE_SIZE];
    uint32_t head;
    uint32_t tail;
    int read_refs;   // Number of read-end references
    int write_refs;  // Number of write-end references
} pipe_t;

/**
 * @brief Creates a new pipe instance.
 * @return Pointer to the newly allocated pipe_t.
 */
pipe_t* create_pipe(void);

/**
 * @brief Destroys a pipe, freeing its allocated resources.
 * @param p Pointer to the pipe to be destroyed.
 */
void destroy_pipe(pipe_t *p);

/**
 * @brief Reads data from a pipe into a buffer.
 * @param p Pointer to the pipe.
 * @param buf Buffer to read data into.
 * @param size Maximum number of bytes to read.
 * @return Number of bytes actually read, or a negative error code.
 */
int pipe_read(pipe_t *p, uint8_t *buf, int size);

/**
 * @brief Writes data from a buffer into a pipe.
 * @param p Pointer to the pipe.
 * @param buf Buffer containing data to write.
 * @param size Number of bytes to write.
 * @return Number of bytes actually written, or a negative error code.
 */
int pipe_write(pipe_t *p, const uint8_t *buf, int size);

#endif