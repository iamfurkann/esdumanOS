#ifndef DEVFS_H
#define DEVFS_H

#include "types.h"

typedef int (*dev_read_fn)(uint8_t *buf, int size);
typedef int (*dev_write_fn)(const uint8_t *buf, int size);

typedef struct {
    char name[32];
    dev_read_fn read;
    dev_write_fn write;
} device_node_t;

extern device_node_t dev_table[];
int get_device_idx(const char *name);

#endif // DEVFS_H