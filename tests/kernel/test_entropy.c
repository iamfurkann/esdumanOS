/*
 * File: test_entropy.c
 * Purpose: Entropy pool tests - uniqueness, policy binding, and measurement.
 *
 * This file is part of the esdumanOS test suite.
 *
 * Two of these assertions are the point of the whole module.
 *
 * The uniqueness assertions test the property the callers actually rely on: that
 * no two extractions in a boot can collide, whatever the sources were worth. That
 * is what makes a repeated CBC IV impossible, and it is checked directly rather
 * than inferred from the entropy estimate.
 *
 * The measurement assertion refuses to guess. Under QEMU TCG the interval between
 * PIT interrupts is close to deterministic, so a design that credited the timer as
 * an entropy source would report a healthy pool while holding nothing. Rather than
 * assume either way, the test prints the jitter the machine really produced and
 * asserts only what the design promises - that the timer is credited nothing.
 */
#include "ktest.h"
#include "entropy.h"
#include "crypto.h"
#include "fs.h"
#include "security.h"
#include "libft.h"

#define DRAW_COUNT 64
#define DRAW_SIZE  16

static int bytes_differ(const uint8_t *a, const uint8_t *b, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {
        if (a[i] != b[i]) return 1;
    }
    return 0;
}

/**
 * @brief Formats an unsigned 32-bit value.
 *
 * Not ktest_itoa(): that one takes an int and loops on "n > 0", so any value above
 * INT_MAX comes out as an empty string. TSC deltas routinely exceed that - the
 * first interval after entropy_init() spans the whole of device init - and a
 * measurement that silently prints nothing is worse than no measurement.
 */
static void ktest_utoa(uint32_t n, char *buf) {
    if (n == 0) { buf[0] = '0'; buf[1] = '\0'; return; }
    char temp[16];
    int i = 0;
    while (n > 0) { temp[i++] = (char)('0' + (n % 10)); n /= 10; }
    int j = 0;
    while (i > 0) { buf[j++] = temp[--i]; }
    buf[j] = '\0';
}

/** Reports a labelled unsigned value on both sinks. */
static void print_stat(const char *label, uint32_t value) {
    char buf[16];
    ktest_utoa(value, buf);
    printk("        %s%s\n", label, buf);
}

/** CPUID leaf 1, ECX bit 30 - the ground truth the quality report must match. */
static int cpu_has_rdrand(void) {
    uint32_t ecx_features;
    asm volatile("cpuid" : "=c"(ecx_features) : "a"(1) : "ebx", "edx");
    return (ecx_features >> 30) & 1;
}

/** Length of the hex-encoded salt in a $v1$ shadow line. */
#define SALT_HEX_LEN 32

/**
 * @brief Locates the salt field inside a $v1$ shadow line.
 *
 * The layout create_shadow_entry() emits is
 *   username ":$v1$" iterations "$" salt_hex(32) "$" dk_hex(64) ":" uid
 * so the salt sits between the third and fourth '$'. Counting separators rather
 * than adding up offsets keeps this working when the username length or the
 * iteration count changes - PBKDF2_DEV_ITERATIONS already varies between test
 * and production builds.
 *
 * Assumes the username itself contains no '$', which holds for the names used
 * below.
 *
 * @param line     The shadow line to parse.
 * @param username Expected account name; the line must start with it.
 * @return Pointer to the first salt digit, or 0 when the line is malformed.
 */
static const char *salt_field(const char *line, const char *username) {
    uint32_t name_len = (uint32_t)ft_strlen(username);
    for (uint32_t i = 0; i < name_len; i++) {
        if (line[i] != username[i]) return 0;
    }

    int dollars = 0;
    const char *p = line;
    while (*p) {
        if (*p == '$') {
            dollars++;
            if (dollars == 3) break;
        }
        p++;
    }
    if (dollars != 3) return 0;

    const char *salt = p + 1;
    for (int i = 0; i < SALT_HEX_LEN; i++) {
        char c = salt[i];
        int is_hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        if (!is_hex) return 0;
    }
    if (salt[SALT_HEX_LEN] != '$') return 0;

    return salt;
}

/** Compares two salt fields located by salt_field(). */
static int hex_fields_differ(const char *a, const char *b) {
    for (int i = 0; i < SALT_HEX_LEN; i++) {
        if (a[i] != b[i]) return 1;
    }
    return 0;
}

/**
 * @brief Exercises the entropy pool, its policy binding, and its two consumers.
 *
 * Expected Behavior:
 * - The pool is seeded by init_security(), so extraction is legal by the time
 *   tests run, and the ENTROPY_FAIL branch is only reachable artificially.
 * - Every extraction differs from every previous one, regardless of quality.
 * - The reported quality agrees with whether the CPU actually offers RDRAND.
 * - The timer source is credited zero bits, by construction.
 * - Two encrypted files written with the same key and the same contents receive
 *   different IVs, and both still decrypt - the format did not change.
 * - Two shadow entries receive different salts.
 *
 * Edge Cases Covered:
 * - A refused extraction must not have written a single output byte.
 * - Same username, same password, twice: the salt still differs.
 */
void run_entropy_tests(void) {
    printk("\n--- Entropy Pool Tests ---\n");

    /* --- The pool is up ------------------------------------------------- */

    KTEST_ASSERT(entropy_quality() != ENTROPY_FAIL,
                 "[ENTROPY] init_security() seeded the pool before any consumer ran");

    int buffer_untouched = 0;
    int fail_code = entropy_selftest_fail_path(&buffer_untouched);
    KTEST_ASSERT(fail_code == ENTROPY_FAIL,
                 "[ENTROPY] an unseeded pool returns ENTROPY_FAIL rather than improvising");
    KTEST_ASSERT(buffer_untouched,
                 "[STRICT] [ENTROPY] a refused extraction leaves the output buffer unwritten");

    /* --- Uniqueness: the property the callers depend on ----------------- */

    static uint8_t draws[DRAW_COUNT][DRAW_SIZE];
    int all_ok = 1;
    for (int i = 0; i < DRAW_COUNT; i++) {
        if (generate_random_bytes(draws[i], DRAW_SIZE) == ENTROPY_FAIL) all_ok = 0;
    }
    KTEST_ASSERT(all_ok, "[ENTROPY] 64 consecutive extractions all succeeded");

    int collisions = 0;
    for (int i = 0; i < DRAW_COUNT; i++) {
        for (int j = i + 1; j < DRAW_COUNT; j++) {
            if (!bytes_differ(draws[i], draws[j], DRAW_SIZE)) collisions++;
        }
    }
    KTEST_ASSERT(collisions == 0,
                 "[ENTROPY] no two of 64 extractions collided (uniqueness holds without RDRAND)");

    int degenerate = 0;
    for (int i = 0; i < DRAW_COUNT; i++) {
        int all_zero = 1, all_same = 1;
        for (int b = 0; b < DRAW_SIZE; b++) {
            if (draws[i][b] != 0) all_zero = 0;
            if (draws[i][b] != draws[i][0]) all_same = 0;
        }
        if (all_zero || all_same) degenerate++;
    }
    KTEST_ASSERT(degenerate == 0,
                 "[ENTROPY] no extraction came back all-zero or a single repeated byte");

    entropy_stats_t before, after;
    entropy_get_stats(&before);
    uint8_t scratch[DRAW_SIZE];
    generate_random_bytes(scratch, sizeof(scratch));
    entropy_get_stats(&after);
    KTEST_ASSERT(after.extractions > before.extractions,
                 "[ENTROPY] the uniqueness counter advances on every extraction");

    uint64_t c1 = entropy_next_counter();
    uint64_t c2 = entropy_next_counter();
    KTEST_ASSERT(c2 != c1,
                 "[ENTROPY] the IV derivation counter is monotonic, never reissued");

    /* --- Quality reporting matches the hardware ------------------------- */

    int has_rdrand = cpu_has_rdrand();
    int quality = generate_random_bytes(scratch, sizeof(scratch));

    KTEST_ASSERT(quality != ENTROPY_FAIL,
                 "[ENTROPY] a seeded pool never reports ENTROPY_FAIL");
    KTEST_ASSERT(entropy_quality() == quality,
                 "[ENTROPY] entropy_quality() predicts what an extraction actually returns");

    if (has_rdrand) {
        /* This one the code path guarantees: RDRAND never falls back to the pool. */
        KTEST_ASSERT(quality == ENTROPY_OK,
                     "[ENTROPY] with RDRAND present, extraction reports ENTROPY_OK");
    }

    entropy_get_stats(&after);

    if (!has_rdrand) {
        int consistent = (after.credited_bits >= 256) ? (quality == ENTROPY_OK)
                                                      : (quality == ENTROPY_WEAK);
        KTEST_ASSERT(consistent,
                     "[STRICT] [ENTROPY] the fallback's verdict matches its own credit count");
    }

    /* --- Crediting is bounded, not merely capped ------------------------ */

    /*
     * The per-event cap was not a limit: event counts are unbounded, so the first
     * measured run had 15629 ATA completions earn 2 bits each and credit the pool
     * 31106 bits. A machine without RDRAND would then have reported ENTROPY_OK on
     * the strength of disk traffic alone. The lifetime budget is what makes the
     * estimate bounded, so these are the assertions that keep the pool honest.
     */
    int budgets_respected = 1;
    for (uint32_t s = 0; s < ENTROPY_SRC_N; s++) {
        if (after.credited_by_source[s] > after.budget_by_source[s]) budgets_respected = 0;
    }
    KTEST_ASSERT(budgets_respected,
                 "[STRICT] [ENTROPY] no source credited more bits than its lifetime budget");

    /*
     * The load-bearing consequence: disk and timer traffic together cannot reach
     * the threshold at which the pool would claim cryptographic quality. Only a
     * genuinely unpredictable source - a human at the keyboard - or RDRAND can.
     */
    KTEST_ASSERT(after.budget_by_source[ENTROPY_SRC_TIMER] +
                 after.budget_by_source[ENTROPY_SRC_ATA] < 256,
                 "[STRICT] [ENTROPY] timer and disk budgets combined cannot reach ENTROPY_OK");

    /* --- Measurement: what did the hardware really give us? ------------- */

    KTEST_ASSERT(after.events[ENTROPY_SRC_TIMER] > 0,
                 "[ENTROPY] the timer interrupt is feeding the pool");
    KTEST_ASSERT(after.credited_by_source[ENTROPY_SRC_TIMER] == 0,
                 "[STRICT] [ENTROPY] the periodic timer earned zero entropy bits");

    printk("      [MEASURED] interrupt jitter actually observed:\n");
    print_stat("timer events    : ", after.events[ENTROPY_SRC_TIMER]);
    print_stat("keyboard events : ", after.events[ENTROPY_SRC_KBD]);
    print_stat("ATA events      : ", after.events[ENTROPY_SRC_ATA]);
    print_stat("timer bits/max  : ", after.credited_by_source[ENTROPY_SRC_TIMER]);
    print_stat("kbd   bits/max  : ", after.credited_by_source[ENTROPY_SRC_KBD]);
    print_stat("ATA   bits/max  : ", after.credited_by_source[ENTROPY_SRC_ATA]);
    print_stat("credited total  : ", after.credited_bits);
    print_stat("OK threshold    : ", 256);
    print_stat("distinct dLSB   : ", after.distinct_delta_lsb);
    print_stat("min TSC delta   : ", after.min_delta);
    print_stat("max TSC delta   : ", after.max_delta);
    print_stat("RDRAND present  : ", (uint32_t)has_rdrand);
    print_stat("verdict (0=OK)  : ", (uint32_t)(quality == ENTROPY_OK ? 0 : 1));
    /*
     * A measured distinct-dLSB of 4 out of 32 is not a defect - it is arithmetic.
     * QEMU TCG advances the TSC in multiples of 1000, and 1000 mod 32 == 8, so
     * (delta & 31) can only ever be 0, 8, 16 or 24. The low bits of every delta
     * on this platform carry no information, which is the measurement that the
     * per-source budgets above exist to answer.
     */
    printk("        (dLSB of 4/32 under TCG is arithmetic, not a defect: deltas are\n");
    printk("         multiples of 1000 and 1000 mod 32 == 8, so only 4 values occur.)\n");

    /* --- Consumer 1: CryptoFS IVs -------------------------------------- */

    const char *payload = "same plaintext";
    const uint32_t payload_len = 14;

    int w1 = fs_create_encrypted("iv_a.txt", (const uint8_t *)payload, payload_len,
                                 kernel_master_key, 0);
    int w2 = fs_create_encrypted("iv_b.txt", (const uint8_t *)payload, payload_len,
                                 kernel_master_key, 0);
    KTEST_ASSERT(w1 == 0 && w2 == 0,
                 "[ENTROPY] two encrypted files written with the same key and contents");

    vfs_file_t fa, fb;
    uint8_t iv_a[DRAW_SIZE], iv_b[DRAW_SIZE];
    int iv_read_ok = 0;

    if (fs_open("iv_a.txt", 0, &fa) == 0 && fs_open("iv_b.txt", 0, &fb) == 0) {
        fa.current_offset = 0;
        fb.current_offset = 0;
        /* The IV is the first 16 plaintext bytes of the file; read it raw. */
        if (fs_read_raw(&fa, iv_a, DRAW_SIZE) == DRAW_SIZE &&
            fs_read_raw(&fb, iv_b, DRAW_SIZE) == DRAW_SIZE) {
            iv_read_ok = 1;
        }
    }
    KTEST_ASSERT(iv_read_ok, "[ENTROPY] both encrypted files' IV headers are readable");
    KTEST_ASSERT(iv_read_ok && bytes_differ(iv_a, iv_b, DRAW_SIZE),
                 "[STRICT] [ENTROPY] identical plaintext under one key still got distinct IVs");

    /*
     * The derivation changed; the on-disk format did not. If these two reads fail,
     * the change broke compatibility with files written by earlier builds.
     */
    uint8_t plain_a[24], plain_b[24];
    ft_memset(plain_a, 0, sizeof(plain_a));
    ft_memset(plain_b, 0, sizeof(plain_b));
    int ra = 0, rb = 0;
    if (fs_open("iv_a.txt", 0, &fa) == 0) {
        ra = fs_read_encrypted(&fa, plain_a, payload_len, kernel_master_key);
    }
    if (fs_open("iv_b.txt", 0, &fb) == 0) {
        rb = fs_read_encrypted(&fb, plain_b, payload_len, kernel_master_key);
    }
    KTEST_ASSERT(ra == (int)payload_len && ft_strcmp((char *)plain_a, payload) == 0,
                 "[ENTROPY] derived-IV file decrypts correctly (wire format unchanged)");
    KTEST_ASSERT(rb == (int)payload_len && ft_strcmp((char *)plain_b, payload) == 0,
                 "[ENTROPY] the second derived-IV file decrypts correctly too");

    fs_delete("iv_a.txt", 0);
    fs_delete("iv_b.txt", 0);

    /* --- Consumer 2: shadow salts -------------------------------------- */

    char line_a[256], line_b[256], line_c[256];
    int s1 = create_shadow_entry("salt_user_a", "pw", 4001, line_a, sizeof(line_a));
    int s2 = create_shadow_entry("salt_user_b", "pw", 4002, line_b, sizeof(line_b));
    int s3 = create_shadow_entry("salt_user_a", "pw", 4001, line_c, sizeof(line_c));

    KTEST_ASSERT(s1 == 0 && s2 == 0 && s3 == 0,
                 "[ENTROPY] shadow entries created while the pool is usable");

    /*
     * Compare the salt field itself, not the whole line. The derived key depends
     * on the salt, so two differing lines could differ for either reason - and a
     * whole-line comparison would pass even if the salts had collided and only
     * the passwords differed. The layout is
     *   username ":$v1$" iterations "$" salt_hex(32) "$" dk_hex(64) ":" uid
     * so the salt begins after the username, the 5-byte prefix and the iteration
     * count (see create_shadow_entry()).
     */
    const char *sa = salt_field(line_a, "salt_user_a");
    const char *sb = salt_field(line_b, "salt_user_b");
    const char *sc = salt_field(line_c, "salt_user_a");

    KTEST_ASSERT(sa && sb && sc,
                 "[ENTROPY] every shadow line carries a locatable 32-hex-digit salt");
    KTEST_ASSERT(sa && sb && hex_fields_differ(sa, sb),
                 "[ENTROPY] two accounts with the same password got different salts");
    KTEST_ASSERT(sa && sc && hex_fields_differ(sa, sc),
                 "[STRICT] [ENTROPY] the same account created twice got a different salt");
}
