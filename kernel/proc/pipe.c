#include "pipe.h"
#include "stdio.h"

#define MAX_SYSTEM_PIPES 16
static pipe_t pipe_pool[MAX_SYSTEM_PIPES];
static int pipe_active[MAX_SYSTEM_PIPES] = {0};

pipe_t* create_pipe(void) {
    for (int i = 0; i < MAX_SYSTEM_PIPES; i++) {
        if (pipe_active[i] == 0) {
            pipe_active[i] = 1;
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

void destroy_pipe(pipe_t *p) {
    if (!p) return;
    for (int i = 0; i < MAX_SYSTEM_PIPES; i++) {
        if (&pipe_pool[i] == p) {
            pipe_active[i] = 0;
            p->read_refs = 0;
            p->write_refs = 0;
            return;
        }
    }
}

int pipe_read(pipe_t *p, uint8_t *buf, int size) {
    if (!p || !buf) return -1;
    
    if (p->head == p->tail) {
        if (p->write_refs <= 0) return 0; 
        return -11;
    }
    
    int bytes_read = 0;
    while (bytes_read < size && p->head != p->tail) {
        buf[bytes_read++] = p->buffer[p->head % PIPE_SIZE];
        p->head++;
    }
    
    extern void wakeup_tasks(int reason);
    wakeup_tasks(2);
    
    return bytes_read;
}

int pipe_write(pipe_t *p, const uint8_t *buf, int size) {
    if (!p || !buf) return -1;
    
    if (p->tail - p->head >= PIPE_SIZE) {
        if (p->read_refs <= 0) return -1; 
        return -11;
    }
    
    int bytes_written = 0;
    while (bytes_written < size && (p->tail - p->head < PIPE_SIZE)) {
        p->buffer[p->tail % PIPE_SIZE] = buf[bytes_written++];
        p->tail++;
    }
    
    extern void wakeup_tasks(int reason);
    wakeup_tasks(2);
    
    return bytes_written;
}