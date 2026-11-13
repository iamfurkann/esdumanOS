#ifndef ENTROPY_H
#define ENTROPY_H

#include "types.h"

/**
 * @file entropy.h
 * @brief Kernel entropy pool, seeded from RDRAND when available and from
 *        interrupt timing jitter otherwise.
 *
 * RDRAND success is treated as strong by this implementation; fallback
 * timing samples may be predictable, particularly under emulation. The quality
 * return value is a policy classification, not a measurement of min-entropy.
 * Fallback extraction counters distinguish derivation inputs within a boot; hashing or
 * truncating those inputs cannot guarantee collision-free output. CBC IVs also
 * need unpredictability, which a counter alone does not provide.
 *
 * A persisted seed can diversify later boots, but is loaded after disk unlock
 * and does not strengthen keys already generated. Failed writes, power loss,
 * cloned disks and rollback can repeat old state. No entropy is credited merely
 * for loading the seed; ENTROPY_WEAK remains weak.
 */

/**
 * @brief The seed carried across boots: where it lives and how big it is.
 *
 * 32 bytes because that is the width of the pool state; a seed larger than the
 * state it is mixed into would be carrying bytes that cannot make a difference.
 *
 * /var rather than /etc, which is the FHS distinction and not a preference: a
 * seed is state the system rewrites on its own, not configuration anybody edits.
 * It is the same directory the kernel log is written into and for the same
 * reason.
 */
#define ENTROPY_SEED_BYTES 32
#define ENTROPY_SEED_NAME  "random-seed"

/** Accepted by the RDRAND/credited-pool quality policy */
#define ENTROPY_OK    0
/** Fallback used — NOT cryptographic quality; counter-mixed derivation */
#define ENTROPY_WEAK -1
/** Pool not initialised; callers must refuse output. */
#define ENTROPY_FAIL -2

/**
 * @brief Event sources that feed the pool.
 *
 * The distinction that matters is periodic versus not. The PIT fires at a fixed
 * rate, so the interval between two of its interrupts carries no entropy no
 * matter how it is measured, and ENTROPY_SRC_TIMER is credited zero bits for
 * that reason. Its samples are still mixed in - they cannot hurt - but they are
 * never counted. Keyboard and disk events are aperiodic and are credited, with
 * a low per-event cap.
 */
#define ENTROPY_SRC_TIMER 0
#define ENTROPY_SRC_KBD   1
#define ENTROPY_SRC_ATA   2
#define ENTROPY_SRC_N     3

/**
 * @brief Observable pool state, for diagnostics and for the self-tests.
 *
 * Deliberately statistics only: no raw sample ever leaves the pool through
 * here, so a test can ask how much jitter the hardware really produced without
 * exposing material that feeds keys.
 */
typedef struct {
    uint32_t events[ENTROPY_SRC_N];            /**< Events absorbed per source.  */
    uint32_t credited_by_source[ENTROPY_SRC_N];/**< Bits each source earned; each
                                                    is capped by its budget.     */
    uint32_t budget_by_source[ENTROPY_SRC_N];  /**< The ceiling each source may
                                                    ever reach.                  */
    uint32_t credited_bits;         /**< Conservative entropy estimate, bits.   */
    uint32_t distinct_delta_lsb;    /**< Distinct (delta & 31) values seen, 0-32 */
    uint32_t min_delta;             /**< Smallest inter-event TSC delta seen.   */
    uint32_t max_delta;             /**< Largest inter-event TSC delta seen.    */
    uint32_t extractions;           /**< Extractions so far; derivation input counter. */
} entropy_stats_t;

/**
 * @brief Seeds the pool and makes extraction legal.
 *
 * Called from init_security() before anything can ask for random bytes. Mixes
 * the full RTC date and time, the TSC, and a few boot-dependent addresses.
 * Idempotent: a second call re-stirs the pool but does not reset its counters.
 *
 * Cross-boot uniqueness does not come from here. This runs before there is a
 * file system to read a seed out of, so all it has to work with is the RTC and
 * the TSC, and two cold boots inside the same RTC second are not guaranteed to
 * differ on those alone. entropy_load_seed() is the second half, and runs once
 * the disk is mounted.
 */
void entropy_init(void);

/**
 * @brief Mixes the seed left by the previous boot into the pool, then replaces it.
 *
 * Called from init_filesystem_and_vfs() once the disk is mounted and the system
 * paths have their modes, which is as early as it can be: entropy_init() has to
 * run before anything may extract, and it runs long before there is a /var.
 *
 * Two things it deliberately does not do. It credits the seed **no entropy** -
 * bytes written by a pool that never reached cryptographic quality do not become
 * unpredictable by being stored, so entropy_quality() reads exactly the same
 * before and after. And it does not fail: a missing, short or unreadable seed is
 * recorded and the boot continues, because the pool is no worse off than it was
 * without one.
 *
 * Replacement is attempted during this call. It does not establish durable
 * atomic replacement: write failure, power loss or rollback can retain a seed
 * already used. Callers must not infer cross-boot uniqueness from this API.
 */
void entropy_load_seed(void);

/**
 * @brief Writes a fresh seed for the next boot.
 *
 * Called at the three checkpoints that already write state out - sync, halt and
 * reboot - alongside klog_persist(), and once more from entropy_load_seed().
 *
 * @return E_OK, or a negative errno. E_ROFS means the security level forbids
 *         writes, which is IMMUTABLE working rather than a failure. Every caller
 *         carries on either way: a machine that would not halt because it could
 *         not refresh its seed would be the worse bargain.
 */
int entropy_persist_seed(void);

/**
 * @brief Absorbs one timing sample. Safe to call from an interrupt handler.
 *
 * Does arithmetic only - no logging, no allocation, no locking - because it runs
 * in ISR context. Samples land in a ring that the extraction path drains with
 * interrupts masked.
 *
 * @param source One of ENTROPY_SRC_*.
 * @param data   Source-specific value mixed alongside the timestamp (a scancode,
 *               a tick count); 0 when there is nothing useful to add.
 */
void entropy_add_event(uint32_t source, uint32_t data);

/**
 * @brief Reports the quality the next extraction would have, without extracting.
 * @return ENTROPY_OK, ENTROPY_WEAK or ENTROPY_FAIL.
 */
int entropy_quality(void);

/**
 * @brief Copies out the diagnostic counters.
 * @param out Destination; ignored when 0.
 */
void entropy_get_stats(entropy_stats_t *out);

/**
 * @brief Fetches and consumes the next IV derivation counter.
 *
 * Monotonic across the boot. fs_create_encrypted() folds it into every IV so
 * that their derivation inputs differ even when the pool has no entropy.
 * This is not a guarantee of distinct or unpredictable IV outputs.
 *
 * @return The next counter value (until 64-bit wraparound).
 */
uint64_t entropy_next_counter(void);

/**
 * @brief Exercises the ENTROPY_FAIL path and restores the pool afterwards.
 *
 * Test-only. ENTROPY_FAIL is otherwise only reachable before entropy_init(),
 * which cannot be re-entered once the kernel is up, so the branch would
 * otherwise go untested. Kept as a single function inside entropy.c rather than
 * an injectable global, so nothing outside can leave the pool disabled.
 *
 * @param buffer_untouched Set to 1 when the refused call left its output buffer
 *                         completely unwritten, 0 otherwise. Ignored when 0.
 * @return What generate_random_bytes() returned while the pool was disabled.
 */
int entropy_selftest_fail_path(int *buffer_untouched);

/**
 * @brief Generate random bytes.
 *
 * Attempts RDRAND first (bounded retry, 10 attempts per 32-bit word). Falls back
 * to the interrupt-jitter pool otherwise. Counter mixing diversifies the
 * derivation inputs; it does not prove output uniqueness or unpredictability.
 *
 * @param buf    Output buffer
 * @param len    Number of bytes to generate
 * @return ENTROPY_OK for RDRAND success or sufficient credited pool bits;
 *         ENTROPY_WEAK for a fallback pool below that policy threshold;
 *         ENTROPY_FAIL when the pool is not initialised (buf is left untouched).
 */
int generate_random_bytes(uint8_t *buf, uint32_t len);

/**
 * @brief Convert binary bytes to hex string.
 *
 * @param bytes  Input binary data
 * @param len    Number of bytes
 * @param hex_out Output buffer (must be at least len*2 + 1 bytes)
 */
void bytes_to_hex(const uint8_t *bytes, uint32_t len, char *hex_out);

/**
 * @brief Convert hex string to binary bytes.
 *
 * @param hex    Input hex string
 * @param out    Output binary buffer
 * @param out_len Expected number of output bytes
 * @return 0 on success, -1 on invalid hex
 */
int hex_to_bytes(const char *hex, uint8_t *out, uint32_t out_len);

#endif // ENTROPY_H
