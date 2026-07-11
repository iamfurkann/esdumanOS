#include "pipe.h"
#include "stdio.h"

#define MAX_SYSTEM_PIPES 16
static pipe_t pipe_pool[MAX_SYSTEM_PIPES];
static int pipe_active[MAX_SYSTEM_PIPES] = {0};

static char pipe_names[MAX_SYSTEM_PIPES][32] = {0};
static int ft_strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

static void ft_strcpy(char *dest, const char *src) {
    while (*src) { *dest++ = *src++; }
    *dest = '\0';
}

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
                ft_strcpy(pipe_names[i], name);
                break;
            }
        }
    }
    return p;
}

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

int pipe_read(pipe_t *p, uint8_t *buf, int size) {
    if (!p || !buf || size <= 0) return -1;

    if (p->head == p->tail) {
        if (p->write_refs <= 0) return 0; // EOF (Yazan taraf tamamen kapattı)
        return -11; // EAGAIN: Bloke ol, uykuya dal!
    }
    
    int bytes_read = 0;
    while (bytes_read < size && p->head != p->tail) {
        buf[bytes_read++] = p->buffer[p->head % PIPE_SIZE];
        p->head++;
    }
    
    extern void wakeup_tasks(int reason);
    wakeup_tasks(2); // 2 = WAIT_IPC
    
    return bytes_read;
}

int pipe_write(pipe_t *p, const uint8_t *buf, int size) {
    if (!p || !buf || size <= 0) return -1;
    
    if (p->tail - p->head >= PIPE_SIZE) {
        if (p->read_refs <= 0) return -1; // EPIPE (Okuyan kimse kalmadı, kırık pipe)
        return -11; // EAGAIN: Bloke ol, uykuya dal!
    }
    
    int bytes_written = 0;
    while (bytes_written < size && (p->tail - p->head < PIPE_SIZE)) {
        p->buffer[p->tail % PIPE_SIZE] = buf[bytes_written++];
        p->tail++;
    }

    extern void wakeup_tasks(int reason);
    wakeup_tasks(2); // 2 = WAIT_IPC
    
    return bytes_written;
}