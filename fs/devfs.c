/*
 * File: devfs.c
 * Purpose: Implementation of virtual device interfaces such as /dev/null,
 *          /dev/random and /dev/urandom.
 *
 * This file is part of the esdumanOS test suite.
 *
 * /dev/random used to seed itself. It ran its own CPUID/RDRAND check and, when
 * RDRAND was absent, keyed a ChaCha20 stream from RDTSC, two RTC registers and
 * the tick count - read once, at first use, and never refreshed. That is exactly
 * the twenty-to-thirty-bit seed the K-9 audit found behind the CryptoFS IVs and
 * the shadow salts, with the difference that this one could be read straight
 * from user space.
 *
 * The output stage is still ChaCha20; what changed is where its key comes from.
 * Every key is now drawn from the kernel entropy pool (entropy.h), which is the
 * one place that decides what this machine's randomness is actually worth and
 * says so. The stream is also re-keyed periodically rather than once per boot,
 * so a machine that only becomes unpredictable later - somebody starts typing,
 * and the keyboard budget carries the pool over its threshold - stops serving
 * output derived from the boot-time draw without needing a reboot.
 */
#include "devfs.h"
#include "errno.h"
#include "klog.h"
#include "libft.h"
#include "rtc.h"
#include "chacha20.h"
#include "entropy.h"

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

/* ── /dev/random and /dev/urandom ──────────────────────────────────── */

/*
 * Re-keying thresholds; whichever is reached first triggers a fresh draw from
 * the pool.
 *
 * The byte count bounds how much output any one key ever produces. The tick
 * count bounds how long a key survives on an idle system, which is what lets
 * entropy that arrives after boot reach a reader that only polls occasionally.
 * The PIT runs at TIMER_HZ, so 500 ticks is about five seconds.
 */
#define DRBG_REKEY_BYTES 4096
#define DRBG_REKEY_TICKS (TIMER_HZ * 5)

static struct {
    chacha20_ctx_t ctx;
    uint32_t bytes_served;  /**< Output produced under the current key.        */
    uint32_t rekey_tick;    /**< timer_get_ticks() at the last re-key.         */
    int      quality;       /**< Pool verdict when the current key was drawn.  */
    int      keyed;         /**< 0 until the pool has supplied a key.          */
} drbg;

/**
 * @brief Draws a fresh ChaCha20 key from the kernel entropy pool.
 *
 * The pool bytes are XORed over a block of the outgoing keystream rather than
 * simply replacing it. A re-key can then only ever add uncertainty: if the pool
 * has nothing new to offer this time the generator is no worse off than it was,
 * and if it does, the new key is at least as unpredictable as those bytes.
 *
 * On ENTROPY_FAIL the existing key is left in place untouched - a pool that
 * cannot serve is a reason to refuse, not a reason to install a known key.
 *
 * @return E_OK when a new key was installed, E_IO when the pool refused.
 */
static int drbg_rekey(void) {
    /* 32-byte key followed by an 8-byte nonce. Aligned because chacha20_init()
     * reads both through uint32_t pointers. */
    uint8_t seed[40] __attribute__((aligned(4)));

    int quality = generate_random_bytes(seed, sizeof(seed));
    if (quality == ENTROPY_FAIL) {
        ft_memset(seed, 0, sizeof(seed));
        return E_IO;
    }

    if (drbg.keyed) {
        uint8_t carry[64] __attribute__((aligned(4)));
        chacha20_next_block(&drbg.ctx, carry);
        for (uint32_t i = 0; i < sizeof(seed); i++) seed[i] ^= carry[i];
        ft_memset(carry, 0, sizeof(carry));
    }

    chacha20_init(&drbg.ctx, seed, seed + 32);
    drbg.bytes_served = 0;
    drbg.rekey_tick   = timer_get_ticks();
    drbg.quality      = quality;
    drbg.keyed        = 1;

    ft_memset(seed, 0, sizeof(seed));
    return E_OK;
}

/**
 * @brief Decides whether the current key has run its course.
 *
 * @return 1 when a fresh draw from the pool is due, 0 otherwise.
 */
static int drbg_needs_rekey(void) {
    if (!drbg.keyed) return 1;
    if (drbg.bytes_served >= DRBG_REKEY_BYTES) return 1;

    /* Unsigned subtraction, so the tick counter wrapping does not stall the
     * re-key for the rest of the boot. */
    if ((uint32_t)(timer_get_ticks() - drbg.rekey_tick) >= DRBG_REKEY_TICKS) return 1;

    /*
     * The current key was drawn while the pool had nothing to offer, and the
     * pool has since crossed its threshold. Take the better material now rather
     * than serving boot-time output until the interval happens to expire.
     */
    if (drbg.quality != ENTROPY_OK && entropy_quality() == ENTROPY_OK) return 1;

    return 0;
}

/**
 * @brief Produces len bytes of keystream, then ratchets the key forward.
 *
 * The ratchet is what gives backtracking resistance: the context is re-keyed
 * from its own output before returning, so a later disclosure of it - a memory
 * dump, a page freed without being wiped - does not reproduce bytes that have
 * already been handed to somebody. The entropy pool does the same to its own
 * state after every extraction, for the same reason.
 *
 * @param buf Destination; must have room for len bytes.
 * @param len Number of bytes to produce.
 */
static void drbg_fill(uint8_t *buf, uint32_t len) {
    uint8_t block[64] __attribute__((aligned(4)));
    uint32_t offset = 0;

    while (offset < len) {
        chacha20_next_block(&drbg.ctx, block);
        for (uint32_t i = 0; i < sizeof(block) && offset < len; i++) {
            buf[offset++] = block[i];
        }
    }

    chacha20_next_block(&drbg.ctx, block);
    chacha20_init(&drbg.ctx, block, block + 32);
    ft_memset(block, 0, sizeof(block));

    drbg.bytes_served += len;
}

/**
 * @brief Shared body of /dev/random and /dev/urandom.
 *
 * No locking: the kernel is not preemptible and no interrupt handler reads a
 * random device, so the generator state cannot be entered twice. The one call
 * that does need interrupts masked - draining the sample ring - masks them
 * inside the entropy pool.
 *
 * @param buf            Destination buffer.
 * @param size           Bytes requested.
 * @param warn_when_weak Non-zero for the device whose name promises more than a
 *                       WEAK pool can deliver; see dev_random_read().
 * @return size on success, or a negative errno.
 */
static int random_device_read(uint8_t *buf, int size, int warn_when_weak) {
    if (!buf) return E_FAULT;
    if (size < 0) return E_INVAL;
    if (size == 0) return 0;

    /* Only refuse when the pool declined AND we have never held a key: with a
     * key already installed the generator remains as good as it was. */
    if (drbg_needs_rekey() && drbg_rekey() != E_OK && !drbg.keyed) {
        /*
         * The pool is not seeded, so anything produced here would be a constant
         * derived from zeroes. Refuse.
         *
         * E_IO rather than E_AGAIN on purpose: sys_read() reads E_AGAIN from a
         * device as "a pipe would block", parks the caller on WAIT_IPC and
         * re-runs the read when IPC traffic wakes it - which nothing here would
         * ever do. A refusal has to be something the caller can actually see.
         */
        klog(LOG_LEVEL_ERROR, "DEVFS",
             "Refused a random device read: the entropy pool is not seeded");
        return E_IO;
    }

    if (warn_when_weak && drbg.quality == ENTROPY_WEAK) {
        /* Once per boot, not once per read: a warning that repeats on every
         * read is a warning nobody reads. */
        static int warned = 0;
        if (!warned) {
            warned = 1;
            klog(LOG_LEVEL_WARN, "DEVFS",
                 "/dev/random is serving entropy pool output without RDRAND: "
                 "unique per read, but not cryptographic-grade entropy");
        }
    }

    drbg_fill(buf, (uint32_t)size);
    return size;
}

/**
 * @brief Reads random bytes from /dev/random.
 *
 * Serves ChaCha20 output keyed from the kernel entropy pool. It refuses only
 * when the pool is unseeded (E_IO), it never blocks, and it does not refuse on
 * ENTROPY_WEAK.
 *
 * That last point is a decision worth recording, because the obvious
 * alternative is the historical Unix one - block, or refuse, until the pool
 * holds enough credited bits. It was rejected on three grounds:
 *
 *   The only source this pool credits meaningfully is the keyboard. The PIT is
 *   periodic and earns nothing by construction, and ATA is held to 64 bits by
 *   its lifetime budget, so timer and disk traffic together can never reach the
 *   256-bit threshold. A blocking /dev/random would therefore block forever on
 *   any machine with nobody at the keyboard - which is every machine that runs
 *   the test suite, and most machines that would run this kernel at all. Linux
 *   abandoned blocking in 5.6 over the same failure mode.
 *
 *   The kernel's own consumers already accept ENTROPY_WEAK. CryptoFS IVs and
 *   /etc/shadow salts are derived from exactly these bytes and proceed with a
 *   warning. Holding /dev/random to a stricter standard than the pool's other
 *   callers would not make the system safer; it would only mean the same
 *   material is judged acceptable in one place and refused in another.
 *
 *   ENTROPY_WEAK does not mean "no randomness". It means unique per extraction
 *   but not provably unpredictable (see entropy.h), which is the correct answer
 *   for every non-cryptographic caller and an honest one for the rest.
 *
 * What remains is honesty about the gap. The name /dev/random says the caller
 * wanted the strict device, so the kernel logs once per boot when it cannot
 * deliver one. User space currently has no way to read that verdict for itself;
 * giving it one means a syscall over entropy_quality(), which is a separate
 * change from this file.
 *
 * @param buf Pointer to the buffer to store random bytes.
 * @param size Number of bytes to read.
 * @return Number of bytes generated, or a negative errno.
 */
int dev_random_read(uint8_t *buf, int size) {
    return random_device_read(buf, size, 1);
}

/**
 * @brief Reads random bytes from /dev/urandom.
 *
 * The same generator, and by the argument in dev_random_read() the same bytes.
 * What differs is what the caller asked for: /dev/urandom is the conventional
 * name for "best effort, do not stop to argue", so a WEAK pool is the expected
 * case here rather than a mismatch worth logging.
 *
 * The device exists because that intent is worth being able to express. A
 * program that opens /dev/urandom has said it will take whatever the machine
 * has; one that opens /dev/random has said it wants the strong device, and the
 * kernel log now records when it did not get it.
 *
 * @param buf Pointer to the buffer to store random bytes.
 * @param size Number of bytes to read.
 * @return Number of bytes generated, or a negative errno.
 */
int dev_urandom_read(uint8_t *buf, int size) {
    return random_device_read(buf, size, 0);
}

/**
 * @brief Attempts to write to a random device, which is read-only.
 *
 * Linux would mix written bytes into the pool without crediting them. That is
 * deliberately not done here: keeping the devices read-only means nothing
 * outside the kernel can influence the generator's state at all, which is a
 * smaller thing to have to reason about than an uncredited mixing path.
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
    {"urandom", dev_urandom_read, dev_random_write},
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
    /* A missing device is an ordinary lookup result, not a fault: sys_open()
     * turns it into E_NOENT for the caller exactly like any other missing path.
     * Logging it at ERROR made passing negative tests look like failures. */
    klog(LOG_LEVEL_DEBUG, "DEVFS", "Device not found");
    return E_NOENT;
}

/**
 * @brief Counts the registered devices, excluding the terminating sentinel.
 *
 * @return Number of usable indices into dev_table.
 */
int get_device_count(void) {
    int n = 0;
    while (dev_table[n].name[0] != '\0') n++;
    return n;
}

/**
 * @brief Checks that an index actually addresses a registered device.
 *
 * Callers store device indices in file descriptors, so a stale or negative
 * value must never reach dev_table[] — indexing it out of range would call
 * through whatever lies around the table.
 *
 * @param idx Candidate index.
 * @return 1 when idx addresses a registered device, 0 otherwise.
 */
int dev_index_is_valid(int idx) {
    return idx >= 0 && idx < get_device_count();
}
