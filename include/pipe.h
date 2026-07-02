#ifndef PIPE_H
#define PIPE_H

#include "types.h"
#include "registers.h" 

#define PIPE_SIZE 4096

typedef struct {
    uint8_t buffer[PIPE_SIZE];
    uint32_t head;
    uint32_t tail;
    int read_refs;   // Okuma ucu referans sayısı
    int write_refs;  // Yazma ucu referans sayısı
} pipe_t;

pipe_t* create_pipe(void);
void destroy_pipe(pipe_t *p);
int pipe_read(pipe_t *p, uint8_t *buf, int size);
int pipe_write(pipe_t *p, const uint8_t *buf, int size);

#endif