#include "devfs.h"

int dev_null_read(uint8_t *buf, int size) { 
    (void)buf;
    (void)size;
    return 0;
}

int dev_null_write(const uint8_t *buf, int size) { 
    (void)buf;
    return size;
}

static uint32_t prng_state = 0xBADF00D; 
int dev_random_read(uint8_t *buf, int size) {
    for(int i = 0; i < size; i++) {
        prng_state = (prng_state * 1103515245 + 12345);
        buf[i] = (prng_state >> 16) & 0xFF;
    }
    return size; 
}

int dev_random_write(const uint8_t *buf, int size) { 
    (void)buf;
    (void)size;
    return -1;
}

device_node_t dev_table[] = {
    {"null", dev_null_read, dev_null_write},
    {"random", dev_random_read, dev_random_write},
    {"", 0, 0}
};

int get_device_idx(const char *name) {
    extern int ft_strcmp(const char*, const char*);
    for(int i = 0; dev_table[i].name[0] != '\0'; i++) {
        if (ft_strcmp(dev_table[i].name, name) == 0) return i;
    }
    return -1;
}