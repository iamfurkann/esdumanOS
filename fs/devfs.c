#include "devfs.h"
#include "io.h"
#include "errno.h"
#include "klog.h"

int dev_null_read(uint8_t *buf, int size) { 
    (void)buf;
    (void)size;
    return 0;
}

int dev_null_write(const uint8_t *buf, int size) { 
    (void)buf;
    return size;
}

static int dev_check_rdrand(void) {
    uint32_t ecx;
    asm volatile("mov $1, %%eax\n cpuid\n" : "=c"(ecx) :: "eax", "ebx", "edx");
    return (ecx & (1 << 30)) != 0;
}

static uint32_t dev_get_hardware_rand(void) {
    uint32_t val;
    uint8_t ok;
    for (int i = 0; i < 10; i++) {
        asm volatile("rdrand %0; setc %1" : "=r"(val), "=qm"(ok));
        if (ok) return val;
    }
    return 0;
}

static uint32_t prng_state = 0;
static int prng_initialized = 0;

int dev_random_read(uint8_t *buf, int size) {
    if (dev_check_rdrand()) {
        int i = 0;
        while (i < size) {
            uint32_t r = dev_get_hardware_rand();
            buf[i++] = r & 0xFF;
            if (i < size) buf[i++] = (r >> 8) & 0xFF;
            if (i < size) buf[i++] = (r >> 16) & 0xFF;
            if (i < size) buf[i++] = (r >> 24) & 0xFF;
        }
        return size;
    }

    if (!prng_initialized) {
        extern uint32_t timer_get_ticks(void);
        uint32_t lo, hi;
        asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
        
        outb(0x70, 0x00); uint8_t s = inb(0x71);
        outb(0x70, 0x02); uint8_t m = inb(0x71);
        
        prng_state = timer_get_ticks() ^ lo ^ hi ^ (s << 16) ^ (m << 8);
        prng_initialized = 1;
    }

    for(int i = 0; i < size; i++) {
        prng_state = (prng_state * 1103515245 + 12345);
        buf[i] = (prng_state >> 16) & 0xFF;
    }
    return size; 
}

int dev_random_write(const uint8_t *buf, int size) { 
    (void)buf;
    (void)size;
    klog(LOG_LEVEL_WARN, "DEVFS", "Attempted write to read-only random device");
    return E_PERM;
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
    klog(LOG_LEVEL_ERROR, "DEVFS", "Device not found");
    return E_NOENT;
}