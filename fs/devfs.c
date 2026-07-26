/*
 * File: devfs.c
 * Purpose: Implementation of virtual device interfaces such as /dev/null and /dev/random.
 *
 * This file is part of the esdumanOS test suite.
 */
#include "devfs.h"
#include "io.h"
#include "errno.h"
#include "klog.h"
#include "libft.h"
#include "rtc.h"
#include "chacha20.h"

/**
 * @brief Reads from the null device. Always returns 0 (EOF).
 *
 * @param buf Pointer to the buffer where data would be stored.
 * @param size Number of bytes to read.
 * @return Always returns 0.
 */
int dev_null_read(uint8_t *buf, int size) { 
    (void)buf;
    (void)size;
    return 0;
}

/**
 * @brief Writes to the null device. Data is discarded.
 *
 * @param buf Pointer to the data to write.
 * @param size Number of bytes to write.
 * @return The number of bytes purportedly written.
 */
int dev_null_write(const uint8_t *buf, int size) { 
    (void)buf;
    return size;
}

/**
 * @brief Checks if the CPU supports the RDRAND instruction.
 *
 * @return 1 if supported, 0 otherwise.
 */
static int dev_check_rdrand(void) {
    uint32_t ecx;
    asm volatile("mov $1, %%eax\n cpuid\n" : "=c"(ecx) :: "eax", "ebx", "edx");
    return (ecx & (1 << 30)) != 0;
}

/**
 * @brief Generates a random number using hardware RDRAND instruction.
 *
 * @return A 32-bit random hardware-generated number, or 0 if generation fails.
 */
static uint32_t dev_get_hardware_rand(void) {
    uint32_t val;
    uint8_t ok;
    for (int i = 0; i < 10; i++) {
        asm volatile("rdrand %0; setc %1" : "=r"(val), "=qm"(ok));
        if (ok) return val;
    }
    return 0;
}

static int prng_initialized = 0;


/**
 * @brief Reads random bytes from the random device. Uses hardware RDRAND if available, else PRNG.
 *
 * @param buf Pointer to the buffer to store random bytes.
 * @param size Number of bytes to read.
 * @return Number of random bytes generated.
 */
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

    static chacha20_ctx_t prng_ctx;
    static uint8_t prng_stream[64];
    static int stream_pos = 64;

    if (!prng_initialized) {
        uint32_t lo, hi;
        asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
        
        outb(0x70, 0x00); uint8_t s = inb(0x71);
        outb(0x70, 0x02); uint8_t m = inb(0x71);
        
        uint32_t entropy1 = timer_get_ticks() ^ lo ^ (s << 16) ^ m;
        uint32_t entropy2 = hi ^ (m << 16) ^ s;
        
        uint8_t key[32] = {0};
        uint8_t nonce[8] = {0};
        ((uint32_t*)key)[0] = entropy1;
        ((uint32_t*)key)[1] = entropy2;
        ((uint32_t*)key)[2] = lo;
        ((uint32_t*)key)[3] = hi;
        
        chacha20_init(&prng_ctx, key, nonce);
        prng_initialized = 1;
    }

    for(int i = 0; i < size; i++) {
        if (stream_pos >= 64) {
            chacha20_next_block(&prng_ctx, prng_stream);
            stream_pos = 0;
        }
        buf[i] = prng_stream[stream_pos++];
    }
    return size; 
}

/**
 * @brief Attempts to write to the random device, which is read-only.
 *
 * @param buf Pointer to data to write.
 * @param size Number of bytes to write.
 * @return E_PERM (operation not permitted).
 */
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

/**
 * @brief Gets the index of a device in the device table by its name.
 *
 * @param name The name of the device to search for.
 * @return Device index on success, or E_NOENT if not found.
 */
int get_device_idx(const char *name) {
for(int i = 0; dev_table[i].name[0] != '\0'; i++) {
        if (ft_strcmp(dev_table[i].name, name) == 0) return i;
    }
    klog(LOG_LEVEL_ERROR, "DEVFS", "Device not found");
    return E_NOENT;
}