/*
 * File: entropy.c
 * Purpose: Kernel entropy pool - collection from interrupt timing, mixing, and
 *          extraction.
 *
 * This file is part of the esdumanOS test suite.
 *
 * The pool exists because generate_random_bytes() used to read every one of its
 * inputs at the moment it was called: RDTSC, three RTC registers, and the address
 * of a local variable. Nothing was collected over time, so the whole thing had
 * perhaps twenty to thirty bits of real uncertainty - and it fed both the
 * /etc/shadow salts and every CryptoFS AES-CBC IV.
 *
 * Two things are separated here, because they carry different guarantees.
 *
 * Unpredictability is best-effort. RDRAND gives it outright. Without RDRAND it
 * comes from interrupt jitter, and on an emulated machine that may amount to
 * almost nothing; the return code says which case we are in and the callers act
 * on it.
 *
 * Uniqueness is unconditional. Every extraction bumps `seq` and folds it into the
 * output, so two extractions in the same boot cannot collide even with every
 * source dead. That is the property fs_create_encrypted() and create_shadow_entry()
 * actually need, and it no longer depends on how good the sources were.
 *
 * Uniqueness across boots is what entropy_load_seed() is for. It used to rest on
 * the RTC timestamp and the TSC that entropy_init() mixes in, so two cold boots
 * of the same image inside the same RTC second were not provably distinct - and
 * a disk image copied to two machines started them both from the same state.
 * A seed carried on disk closes that, and closes only that: the bytes are mixed
 * in and credited nothing, because a seed written by a pool that never reached
 * cryptographic quality does not arrive at the next boot with any more than it
 * left with. Unpredictability still comes from RDRAND or from nowhere.
 */
#include "types.h"
#include "io.h"
#include "klog.h"
#include "crypto.h"
#include "libft.h"
#include "fs.h"
#include "errno.h"
#include "entropy.h"

#define CMOS_ADDRESS 0x70
#define CMOS_DATA    0x71

/** Ring of raw samples written by interrupt handlers. Power of two. */
#define ENTROPY_RING_SIZE 64

/**
 * Credited bits needed before extraction claims ENTROPY_OK.
 *
 * 256 because that is the size of the pool state and of the keys derived from it;
 * claiming cryptographic quality on less would be claiming more than we hold.
 */
#define ENTROPY_OK_THRESHOLD 256

/** Per-event ceiling on credited bits, by source. Timer is deliberately zero. */
#define ENTROPY_CREDIT_TIMER 0
#define ENTROPY_CREDIT_KBD   4
#define ENTROPY_CREDIT_ATA   2

/*
 * Lifetime budgets: the most a source may ever contribute, however many events it
 * produces.
 *
 * A per-event cap alone is not a limit, because event counts are unbounded. The
 * first measured run made that concrete: 15629 ATA completions each earned the
 * full 2 bits, so the pool credited itself 31106 bits - roughly four kilobytes of
 * "entropy" - and a machine with no RDRAND would then have reported ENTROPY_OK
 * while holding nothing of the sort. The same run showed why the timing is worth
 * so much less than it looks: every TSC delta was a multiple of 1000, so only 4
 * of the 32 possible values of (delta & 31) ever appeared. The low bits carry no
 * information at all on this platform.
 *
 * So each source gets a budget matched to whether its unpredictability actually
 * scales with event count:
 *
 *   TIMER  0     Periodic. Contributes nothing, at any volume.
 *   ATA    64    Completion timing is partly device- and partly emulator-driven,
 *                and a boot that happens to do more I/O is not a boot with more
 *                secrets. A token amount, deliberately far below the threshold,
 *                so disk traffic alone can never claim cryptographic quality.
 *   KBD    4096  Human keystroke intervals genuinely do scale - each press is an
 *                independent event this machine cannot predict.
 *
 * The consequence is intended: without RDRAND and without someone typing, the
 * pool stays at ENTROPY_WEAK and says so.
 */
#define ENTROPY_BUDGET_TIMER 0
#define ENTROPY_BUDGET_KBD   4096
#define ENTROPY_BUDGET_ATA   64

/*
 * Single CPU (get_current_cpu_id() is a constant 0) and a non-preemptible
 * kernel, so no lock is needed: interrupt handlers only ever append to the ring,
 * and the extraction path drains it with interrupts masked.
 */
static struct {
    uint8_t  state[32];
    uint32_t ring[ENTROPY_RING_SIZE];
    uint32_t ring_head;
    uint32_t ring_count;

    uint32_t seq;
    uint64_t counter;
    uint32_t credited_bits;
    uint32_t events[ENTROPY_SRC_N];
    uint32_t credited_by_source[ENTROPY_SRC_N];

    uint32_t last_tsc;
    uint32_t last_d1;
    uint32_t last_d2;

    uint32_t delta_lsb_map;
    uint32_t min_delta;
    uint32_t max_delta;

    uint8_t  seeded;
} pool;

static inline uint32_t read_tsc_low(void) {
    uint32_t lo, hi;
    asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
    (void)hi;
    return lo;
}

static inline uint8_t get_cmos_register(uint8_t reg) {
    outb(CMOS_ADDRESS, reg);
    return inb(CMOS_DATA);
}

/** Number of significant bits in v; 0 for v == 0. */
static uint32_t bit_length(uint32_t v) {
    uint32_t n = 0;
    while (v) { n++; v >>= 1; }
    return n;
}

static uint32_t popcount32(uint32_t v) {
    uint32_t n = 0;
    while (v) { n += (v & 1); v >>= 1; }
    return n;
}

/**
 * @brief Absorbs a 32-bit word into the pool state.
 *
 * state = SHA256(state || word). Cheap enough to call a handful of times per
 * extraction, and it is never called from interrupt context - handlers append to
 * the ring instead.
 */
static void pool_absorb_word(uint32_t word) {
    sha256_ctx_t ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, pool.state, sizeof(pool.state));
    sha256_update(&ctx, (const uint8_t *)&word, sizeof(word));
    sha256_final(&ctx, pool.state);
}

void entropy_init(void) {
    uint32_t seed[10];
    volatile uint32_t stack_probe = 0;

    seed[0] = read_tsc_low();
    seed[1] = get_cmos_register(0x00);              /* seconds       */
    seed[2] = get_cmos_register(0x02);              /* minutes       */
    seed[3] = get_cmos_register(0x04);              /* hours         */
    seed[4] = get_cmos_register(0x07);              /* day of month  */
    seed[5] = get_cmos_register(0x08);              /* month         */
    seed[6] = get_cmos_register(0x09);              /* year          */
    seed[7] = (uint32_t)&stack_probe;
    seed[8] = (uint32_t)&pool;
    seed[9] = read_tsc_low();

    if (!pool.seeded) {
        ft_memset(&pool, 0, sizeof(pool));
        pool.min_delta = 0xFFFFFFFFu;
    }

    for (uint32_t i = 0; i < 10; i++) {
        pool_absorb_word(seed[i]);
    }

    pool.last_tsc = seed[9];
    pool.seeded = 1;
}

/**
 * @brief Absorbs a run of bytes into the pool state.
 *
 * state = SHA256(state || label || data). The label is there so that bytes that
 * arrive as a stored seed cannot be confused with bytes that arrive any other
 * way, which costs nothing and makes the domains separate on purpose rather
 * than by accident.
 */
static void pool_absorb_bytes(const char *label, const uint8_t *data, uint32_t len) {
    sha256_ctx_t ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, pool.state, sizeof(pool.state));
    sha256_update(&ctx, (const uint8_t *)label, (uint32_t)ft_strlen(label));
    sha256_update(&ctx, data, len);
    sha256_final(&ctx, pool.state);
}

/**
 * @brief Locates /var, which is where the seed lives.
 * @return Entry id of /var, or a negative errno.
 */
static int entropy_seed_dir(void) {
    int var_id = fs_get_entry_idx("var", 0);
    return (var_id < 0) ? E_NOENT : var_id;
}

int entropy_persist_seed(void) {
    uint8_t seed[ENTROPY_SEED_BYTES];

    /*
     * ENTROPY_FAIL leaves the buffer untouched, so there would be nothing to
     * write but the stack's previous contents. Nothing is written instead - a
     * seed file holding whatever happened to be on the stack is worse than no
     * seed file, because the next boot would mix it in and believe it.
     */
    if (generate_random_bytes(seed, sizeof(seed)) == ENTROPY_FAIL) {
        klog(LOG_LEVEL_WARN, "ENTROPY", "The pool is not seeded; no seed was written for the next boot.");
        return E_IO;
    }

    int var_id = entropy_seed_dir();
    if (var_id < 0) {
        ft_memset(seed, 0, sizeof(seed));
        klog(LOG_LEVEL_WARN, "ENTROPY", "/var is missing; no seed was written for the next boot.");
        return var_id;
    }

    /* klog_persist()'s shape, and for its reason: a stored file is one blob that
     * can be replaced but not extended. */
    int rc;
    int existing = fs_get_entry_idx(ENTROPY_SEED_NAME, (fs_id_t)var_id);

    if (existing >= 0) {
        rc = fs_atomic_update(ENTROPY_SEED_NAME, seed, sizeof(seed), (fs_id_t)var_id);
    } else {
        rc = fs_create_file(ENTROPY_SEED_NAME, seed, sizeof(seed), (fs_id_t)var_id);

        /*
         * A new file takes FS_MODE_DEFAULT_FILE, which is 0644, and a seed every
         * user can read is a seed that buys nothing: knowing it is knowing where
         * the pool started. The mode is set on creation and enforced again on
         * every boot by apply_system_modes(), for /etc/shadow's reason - the
         * boot-time pass is what covers a disk that arrived with the wrong one.
         *
         * Only the creating path needs this. fs_atomic_update() swaps the
         * contents underneath the existing entry and leaves its mode and owner
         * alone, which is what makes it an update rather than a new file.
         */
        if (rc == E_OK) {
            int idx = fs_get_entry_idx(ENTROPY_SEED_NAME, (fs_id_t)var_id);
            if (idx >= 0) fs_chmod(dir_table[idx].entry_id, 0600);
        }
    }

    ft_memset(seed, 0, sizeof(seed));

    /*
     * A refusal from the security level is expected rather than wrong: IMMUTABLE
     * exists to stop writes and this is a write, and under LOCKDOWN the master
     * key is gone so a written seed could never be read back. Both are recorded
     * and neither is escalated - the three checkpoints that call this are sync,
     * halt and reboot, and a machine that would not halt because it could not
     * refresh its seed would be the worse bargain, exactly as it is for the log.
     *
     * Two codes because the two write paths disagree about which one to use:
     * fs_create_file() answers E_ROFS and fs_atomic_update() answers E_ACCES for
     * the same IMMUTABLE level. Left alone rather than reconciled here, since
     * every caller of both already treats them the same way.
     */
    if (rc == E_ROFS || rc == E_ACCES) {
        klog(LOG_LEVEL_INFO, "ENTROPY", "The security level forbids writes; the seed was left as it was.");
    } else if (rc != E_OK) {
        klog_int(LOG_LEVEL_WARN, "ENTROPY", "Writing the seed failed with errno", rc);
    }

    return rc;
}

void entropy_load_seed(void) {
    int var_id = entropy_seed_dir();

    if (var_id < 0) {
        klog(LOG_LEVEL_WARN, "ENTROPY", "/var is missing; no seed was carried across the boot.");
        return;
    }

    vfs_file_t seed_file;
    uint8_t seed[ENTROPY_SEED_BYTES];

    /*
     * fs_read() and fs_create_file() both decide by the current security level,
     * so a seed written and read at the same level round-trips exactly. A level
     * raised to CRYPTO_ENFORCED before the write and back down before the read
     * gives 32 bytes of the stored form instead of the plaintext - which is a
     * harmless outcome here, and only here, because distinctness is the entire
     * job and ciphertext is exactly as distinct. Under LOCKDOWN the key is gone
     * and the read refuses outright, which the short-read branch handles.
     */
    if (fs_open(ENTROPY_SEED_NAME, (fs_id_t)var_id, &seed_file) == E_OK) {
        int got = fs_read(&seed_file, seed, sizeof(seed));

        if (got == (int)sizeof(seed)) {
            pool_absorb_bytes("esdumanOS/seed", seed, sizeof(seed));
            klog(LOG_LEVEL_INFO, "ENTROPY", "A seed from the previous boot was mixed in, credited no bits.");
        } else {
            /*
             * Short, unreadable or the wrong length. Not used, and said out loud:
             * mixing a partial seed would still be safe, but a seed file that has
             * silently stopped being a seed file is the kind of thing that goes
             * unnoticed for releases.
             */
            klog_int(LOG_LEVEL_WARN, "ENTROPY", "The stored seed was not the expected size and was not used; read", got);
        }
        ft_memset(seed, 0, sizeof(seed));
    } else {
        klog(LOG_LEVEL_INFO, "ENTROPY", "No stored seed was found; this is a first boot on this disk.");
    }

    /*
     * Replaced now, not at the next checkpoint, and that is the whole guarantee.
     * sync, halt and reboot refresh it too, but a machine that loses power before
     * reaching one of them would otherwise come back up and mix in the very same
     * seed it used last time - two boots from one seed, which is the case this
     * exists to rule out. Writing immediately means the seed on disk has always
     * already been consumed.
     */
    entropy_persist_seed();
}

void entropy_add_event(uint32_t source, uint32_t data) {
    if (source >= ENTROPY_SRC_N) return;

    uint32_t tsc = read_tsc_low();
    uint32_t d1  = tsc - pool.last_tsc;
    uint32_t d2  = d1 - pool.last_d1;
    uint32_t d3  = d2 - pool.last_d2;

    pool.last_tsc = tsc;
    pool.last_d1  = d1;
    pool.last_d2  = d2;

    pool.ring[pool.ring_head] = d1 ^ (data << 16) ^ source ^ tsc;
    pool.ring_head = (pool.ring_head + 1) & (ENTROPY_RING_SIZE - 1);
    if (pool.ring_count < ENTROPY_RING_SIZE) pool.ring_count++;

    pool.events[source]++;

    /* Observed jitter, so the tests can report what the hardware really did. */
    pool.delta_lsb_map |= (1u << (d1 & 31));
    if (d1 < pool.min_delta) pool.min_delta = d1;
    if (d1 > pool.max_delta) pool.max_delta = d1;

    /*
     * Credit the smallest of the first three derivatives, which is what stops a
     * source with a large but regular interval from looking rich: a strictly
     * periodic source has d2 == d3 == 0 and earns nothing. The per-source cap
     * then keeps even a genuinely jittery source honest.
     *
     * The timer is capped at zero outright. It fires from the PIT at a fixed
     * rate, so its intervals are not a source of entropy however they are
     * measured; the samples are mixed in anyway, but never counted.
     */
    uint32_t cap, budget;
    switch (source) {
        case ENTROPY_SRC_KBD: cap = ENTROPY_CREDIT_KBD; budget = ENTROPY_BUDGET_KBD; break;
        case ENTROPY_SRC_ATA: cap = ENTROPY_CREDIT_ATA; budget = ENTROPY_BUDGET_ATA; break;
        default:              cap = ENTROPY_CREDIT_TIMER; budget = ENTROPY_BUDGET_TIMER; break;
    }

    /*
     * No early return for the zero-budget case: letting the timer run through the
     * same arithmetic as everything else costs three bit_length() calls per tick
     * and means the tests are checking a value the pool actually computed rather
     * than a branch that skipped the computation.
     */
    uint32_t b1 = bit_length(d1);
    uint32_t b2 = bit_length(d2);
    uint32_t b3 = bit_length(d3);
    uint32_t credit = b1;
    if (b2 < credit) credit = b2;
    if (b3 < credit) credit = b3;
    if (credit > cap) credit = cap;

    /* Spend against the source's lifetime budget; never past it. */
    uint32_t spent = pool.credited_by_source[source];
    uint32_t remaining = (spent >= budget) ? 0 : (budget - spent);
    if (credit > remaining) credit = remaining;
    if (credit == 0) return;

    pool.credited_by_source[source] = spent + credit;
    pool.credited_bits += credit;
}

/** CPUID leaf 1, ECX bit 30. */
static int cpu_has_rdrand(void) {
    uint32_t ecx_features;
    asm volatile("cpuid" : "=c"(ecx_features) : "a"(1) : "ebx", "edx");
    return (ecx_features >> 30) & 1;
}

int entropy_quality(void) {
    if (!pool.seeded) return ENTROPY_FAIL;

    /*
     * Has to agree with what generate_random_bytes() would actually return, or
     * callers deciding policy from it would be deciding on the wrong thing. That
     * means asking about RDRAND as well: with RDRAND present the pool's credited
     * bits are irrelevant, because the extraction never reaches the pool.
     */
    if (cpu_has_rdrand()) return ENTROPY_OK;
    return (pool.credited_bits >= ENTROPY_OK_THRESHOLD) ? ENTROPY_OK : ENTROPY_WEAK;
}

void entropy_get_stats(entropy_stats_t *out) {
    if (!out) return;

    for (uint32_t i = 0; i < ENTROPY_SRC_N; i++) {
        out->events[i]             = pool.events[i];
        out->credited_by_source[i] = pool.credited_by_source[i];
    }
    out->budget_by_source[ENTROPY_SRC_TIMER] = ENTROPY_BUDGET_TIMER;
    out->budget_by_source[ENTROPY_SRC_KBD]   = ENTROPY_BUDGET_KBD;
    out->budget_by_source[ENTROPY_SRC_ATA]   = ENTROPY_BUDGET_ATA;

    out->credited_bits       = pool.credited_bits;
    out->distinct_delta_lsb  = popcount32(pool.delta_lsb_map);
    out->min_delta           = (pool.min_delta == 0xFFFFFFFFu) ? 0 : pool.min_delta;
    out->max_delta           = pool.max_delta;
    out->extractions         = pool.seq;
}

uint64_t entropy_next_counter(void) {
    uint32_t eflags;
    asm volatile("pushf; pop %0" : "=r"(eflags));
    asm volatile("cli");

    uint64_t value = pool.counter++;

    if (eflags & 0x200) asm volatile("sti");
    return value;
}

/**
 * @brief Drains the ring into the pool state.
 *
 * Runs with interrupts masked so a handler cannot append mid-drain. The old
 * interrupt state is restored rather than unconditionally re-enabled, because
 * this can be reached from a path that had them off.
 */
static void pool_drain_ring(void) {
    uint32_t eflags;
    asm volatile("pushf; pop %0" : "=r"(eflags));
    asm volatile("cli");

    uint32_t samples[ENTROPY_RING_SIZE];
    uint32_t count = pool.ring_count;

    /*
     * The valid samples are ring[0 .. count-1] in both cases: while the ring is
     * filling, head == count; once it is full, every slot holds a sample.
     */
    for (uint32_t i = 0; i < count; i++) samples[i] = pool.ring[i];
    pool.ring_count = 0;
    pool.ring_head  = 0;

    if (eflags & 0x200) asm volatile("sti");

    if (count == 0) return;

    sha256_ctx_t ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, pool.state, sizeof(pool.state));
    sha256_update(&ctx, (const uint8_t *)samples, count * sizeof(uint32_t));
    sha256_final(&ctx, pool.state);
}

/**
 * @brief Fills buf from the pool state in counter mode.
 *
 * block_i = SHA256(state || seq || i), then the state is hashed forward so that
 * recovering it later does not reveal what was handed out before.
 */
static void pool_extract(uint8_t *buf, uint32_t len) {
    uint32_t seq = ++pool.seq;

    uint32_t offset = 0;
    uint32_t index  = 0;
    while (offset < len) {
        uint8_t block[32];
        sha256_ctx_t ctx;
        sha256_init(&ctx);
        sha256_update(&ctx, pool.state, sizeof(pool.state));
        sha256_update(&ctx, (const uint8_t *)&seq, sizeof(seq));
        sha256_update(&ctx, (const uint8_t *)&index, sizeof(index));
        sha256_final(&ctx, block);
        index++;

        for (uint32_t i = 0; i < 32 && offset < len; i++) buf[offset++] = block[i];
        ft_memset(block, 0, sizeof(block));
    }

    /* Forward secrecy: state = SHA256(state || 0x01). */
    uint8_t tag = 0x01;
    sha256_ctx_t ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, pool.state, sizeof(pool.state));
    sha256_update(&ctx, &tag, 1);
    sha256_final(&ctx, pool.state);
}

int generate_random_bytes(uint8_t *buf, uint32_t len) {
    if (!buf || len == 0) return entropy_quality();

    /*
     * Refuse rather than improvise. Before entropy_init() the pool state is all
     * zeroes and seq is 0, so not even uniqueness holds - and a caller that
     * silently accepted those bytes would be writing a predictable salt or IV.
     */
    if (!pool.seeded) return ENTROPY_FAIL;

    int has_rdrand = cpu_has_rdrand();

    if (has_rdrand) {
        uint32_t offset = 0;
        while (offset < len) {
            uint32_t rand_val;
            int success = 0;
            // Bounded retry: max 10 attempts per word
            for (int attempt = 0; attempt < 10; attempt++) {
                uint8_t cf;
                asm volatile(
                    "rdrand %0\n\t"
                    "setc %1\n\t"
                    : "=r"(rand_val), "=qm"(cf)
                    :
                    : "cc"
                );
                if (cf) { success = 1; break; }
            }
            if (!success) {
                // RDRAND failed after retries — fall through to the pool
                has_rdrand = 0;
                break;
            }
            /* Feed the pool too, so a later fallback benefits from this. */
            pool_absorb_word(rand_val);

            for (int b = 0; b < 4 && offset < len; b++) {
                buf[offset++] = (rand_val >> (b * 8)) & 0xFF;
            }
        }
        if (has_rdrand && offset >= len) {
            pool.seq++;   /* keep the uniqueness counter moving on this path too */
            return ENTROPY_OK;
        }
    }

    pool_drain_ring();
    pool_extract(buf, len);

    if (pool.credited_bits >= ENTROPY_OK_THRESHOLD) return ENTROPY_OK;
    return ENTROPY_WEAK;
}

int entropy_selftest_fail_path(int *buffer_untouched) {
    uint8_t scratch[16];
    uint8_t was_seeded = pool.seeded;

    ft_memset(scratch, 0xA5, sizeof(scratch));

    pool.seeded = 0;
    int observed = generate_random_bytes(scratch, sizeof(scratch));
    pool.seeded = was_seeded;

    if (buffer_untouched) {
        /*
         * A caller that refuses on ENTROPY_FAIL keeps whatever was already in
         * its buffer, so the refusal has to happen before a single byte is
         * written - a half-overwritten IV would be worse than an obviously
         * wrong one.
         */
        int clean = 1;
        for (uint32_t i = 0; i < sizeof(scratch); i++) {
            if (scratch[i] != 0xA5) { clean = 0; break; }
        }
        *buffer_untouched = clean;
    }

    return observed;
}
