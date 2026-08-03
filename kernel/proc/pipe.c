/**
 * @file pipe.c
 * @brief Inter-process communication pipes implementation.
 */
#include "pipe.h"
#include "stdio.h"
#include "errno.h"
#include "klog.h"
#include "process.h"
#include "libft.h"
#define MAX_SYSTEM_PIPES 16
static pipe_t pipe_pool[MAX_SYSTEM_PIPES];
static int pipe_active[MAX_SYSTEM_PIPES] = {0};

static char pipe_names[MAX_SYSTEM_PIPES][32] = {0};

/**
 * @brief Create a new unnamed pipe.
 * 
 * @return Pointer to the created pipe_t, or 0 if no pipes are available.
 */
pipe_t* create_pipe(void) {
    for (int i = 0; i < MAX_SYSTEM_PIPES; i++) {
        if (pipe_active[i] == 0) {
            pipe_active[i] = 1;
            pipe_names[i][0] = '\0';
            pipe_t *p = &pipe_pool[i];
            p->head = 0;
            p->tail = 0;
            p->read_refs = 1;
            p->write_refs = 1;
            return p;
        }
    }
    return 0;
}

/**
 * @brief Get an existing named pipe or create a new one.
 * 
 * @param name Name of the pipe.
 * @return Pointer to the pipe_t, or 0 if no pipes are available.
 */
pipe_t* get_or_create_named_pipe(const char *name) {
    for (int i = 0; i < MAX_SYSTEM_PIPES; i++) {
        if (pipe_active[i] == 1 && ft_strcmp(pipe_names[i], name) == 0) {
            return &pipe_pool[i];
        }
    }

    pipe_t *p = create_pipe();
    if (p) {
        for (int i = 0; i < MAX_SYSTEM_PIPES; i++) {
            if (&pipe_pool[i] == p) {
                ft_strlcpy(pipe_names[i], name, 32);
                break;
            }
        }
    }
    return p;
}

/**
 * @brief Destroy a pipe and free its resources.
 * 
 * @param p Pointer to the pipe_t to destroy.
 */
void destroy_pipe(pipe_t *p) {
    if (!p) return;
    for (int i = 0; i < MAX_SYSTEM_PIPES; i++) {
        if (&pipe_pool[i] == p) {
            pipe_active[i] = 0;
            pipe_names[i][0] = '\0'; // İsmi de temizle
            p->read_refs = 0;
            p->write_refs = 0;
            return;
        }
    }
}

/**
 * @brief Read data from a pipe.
 * 
 * @param p Pointer to the pipe_t.
 * @param buf Pointer to the destination buffer.
 * @param size Number of bytes to read.
 * @return Number of bytes read, or a negative error code.
 */
int pipe_read(pipe_t *p, uint8_t *buf, int size) {
    if (!p || !buf || size <= 0) return E_INVAL;

    if (p->head == p->tail) {
        if (p->write_refs <= 0) return 0; // EOF (Yazan taraf tamamen kapattı)
        return E_AGAIN; // EAGAIN: Bloke ol, uykuya dal!
    }
    
    int bytes_read = 0;
    while (bytes_read < size && p->head != p->tail) {
        buf[bytes_read++] = p->buffer[p->head % PIPE_SIZE];
        p->head++;
    }
wakeup_tasks(2); // 2 = WAIT_IPC
    
    return bytes_read;
}

/**
 * @brief Write data to a pipe.
 * 
 * @param p Pointer to the pipe_t.
 * @param buf Pointer to the source buffer.
 * @param size Number of bytes to write.
 * @return Number of bytes written, or a negative error code.
 */
int pipe_write(pipe_t *p, const uint8_t *buf, int size) {
    if (!p || !buf || size <= 0) return E_INVAL;
    
    if (p->tail - p->head >= PIPE_SIZE) {
        if (p->read_refs <= 0) {
            klog(LOG_LEVEL_WARN, "PIPE", "Broken pipe! Write attempted on pipe with no readers.");
            return E_PIPE; // EPIPE
        }
        return E_AGAIN; // EAGAIN: Bloke ol, uykuya dal!
    }
    
    int bytes_written = 0;
    while (bytes_written < size && (p->tail - p->head < PIPE_SIZE)) {
        p->buffer[p->tail % PIPE_SIZE] = buf[bytes_written++];
        p->tail++;
    }
wakeup_tasks(2); // 2 = WAIT_IPC
    
    return bytes_written;
}