#ifndef BCACHE_H
#define BCACHE_H

#include "types.h"

#define BCACHE_SIZE 64 

typedef struct {
    uint32_t sector;
    uint8_t  data[512];
    uint32_t last_access;
    uint8_t  is_valid;
    uint8_t  is_dirty; 
} bcache_node_t;

void bcache_init(void);
void bcache_read_sector(uint32_t sector, uint8_t *buffer);
void bcache_write_sector(uint32_t sector, uint8_t *buffer);
void bcache_flush(void);

#endif // BCACHE_H