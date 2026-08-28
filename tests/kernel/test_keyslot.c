/*
 * File: test_keyslot.c
 * Purpose: The passphrase key slot that guards the disk's data key.
 *
 * This file is part of the esdumanOS test suite.
 *
 * Every assertion here is against crypto/keyslot.c alone: no disk, no mount, no
 * prompt. That is deliberate and it is what the module was shaped for. The
 * pieces this cannot reach - reading the slot out of the superblock, and asking
 * a human for the passphrase - are the two pieces a test cannot honestly cover,
 * and they are covered by hand in QEMU instead.
 *
 * Each keyslot_create() and each keyslot_unwrap() is a full PBKDF2 derivation,
 * so the slots below are built once and reused. Test builds run at
 * PBKDF2_DEV_ITERATIONS, which is what keeps this from dominating the run.
 */
#include "ktest.h"
#include "keyslot.h"
#include "fs.h"
#include "errno.h"
#include "libft.h"
#include "pbkdf2.h"

#define PASS_A "correct horse battery"
#define PASS_B "a different passphrase"

/**
 * @brief Whether two byte runs are identical. Plain comparison; nothing secret.
 */
static int bytes_equal(const uint8_t *a, const uint8_t *b, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) {
        if (a[i] != b[i]) return 0;
    }
    return 1;
}

/**
 * @brief A recognisable data key, so a wrong answer is obviously wrong.
 */
static void fill_test_dek(uint8_t dek[KEYSLOT_KEY_LEN]) {
    for (uint32_t i = 0; i < KEYSLOT_KEY_LEN; i++) {
        dek[i] = (uint8_t)(0xA0 + i);
    }
}

/**
 * @brief Tests the layouts that go on the disk.
 *
 * Both structures are written into sector 0 as flat bytes, so their sizes are
 * format rather than implementation detail - the same argument disk_file_entry_t
 * has had an assertion for since v0.10.0. A field added to either without
 * thinking about the disk moves every byte behind it, and the disk cannot notice.
 */
static void run_layout_assertions(void) {
    KTEST_ASSERT(sizeof(keyslot_t) == KEYSLOT_DISK_SIZE,
                 "[STRICT] [KEYSLOT] a key slot is 116 bytes, as the superblock assumes");
    KTEST_ASSERT(sizeof(disk_superblock_t) == FS_SUPER_DISK_SIZE,
                 "[STRICT] [KEYSLOT] the superblock is 160 bytes with the slot in it");
    KTEST_ASSERT(sizeof(disk_superblock_t) <= 512,
                 "[STRICT] [KEYSLOT] and still fits in the sector it is written to");
}

/**
 * @brief Tests what the two entry points refuse before doing any work.
 *
 * Cheap, and worth asserting rather than assuming: an empty passphrase is what
 * the boot prompt produces when the user presses Enter without typing, and a
 * slot that accepted it would be a disk anybody opens.
 */
static void run_refusal_assertions(const keyslot_t *good) {
    keyslot_t slot;
    uint8_t dek[KEYSLOT_KEY_LEN];
    uint8_t out[KEYSLOT_KEY_LEN];
    char long_pass[KEYSLOT_MAX_PASSPHRASE + 8];

    fill_test_dek(dek);

    KTEST_ASSERT(keyslot_create(&slot, "", dek) == E_INVAL,
                 "[STRICT] [KEYSLOT] an empty passphrase cannot create a slot");
    KTEST_ASSERT(keyslot_create(&slot, 0, dek) == E_INVAL,
                 "[KEYSLOT] nor can a null one");
    KTEST_ASSERT(keyslot_create(0, PASS_A, dek) == E_INVAL,
                 "[KEYSLOT] and there must be somewhere to put it");

    for (uint32_t i = 0; i < sizeof(long_pass) - 1; i++) long_pass[i] = 'x';
    long_pass[sizeof(long_pass) - 1] = '\0';
    KTEST_ASSERT(keyslot_create(&slot, long_pass, dek) == E_INVAL,
                 "[STRICT] [KEYSLOT] a passphrase longer than the prompt can hold is refused");

    KTEST_ASSERT(keyslot_unwrap(good, "", out) == E_INVAL,
                 "[KEYSLOT] an empty passphrase cannot open one either");
}

/**
 * @brief Tests that editing any part of a slot makes it stop opening.
 *
 * The tag covers the salt and the IV as well as the ciphertext, and these are
 * the assertions that say so. Without that binding, an attacker holding the disk
 * could keep the wrapped key and substitute a salt of their own, then open it
 * with a passphrase they chose.
 *
 * No derivation happens until the tag has been checked, so each of these costs
 * one PBKDF2 pass and no more.
 */
static void run_tamper_assertions(const keyslot_t *good) {
    keyslot_t slot;
    uint8_t out[KEYSLOT_KEY_LEN];

    ft_memcpy(&slot, good, sizeof(keyslot_t));
    slot.tag[0] ^= 0x01;
    KTEST_ASSERT(keyslot_unwrap(&slot, PASS_A, out) == E_ACCES,
                 "[STRICT] [KEYSLOT] a slot with an edited tag does not open");

    ft_memcpy(&slot, good, sizeof(keyslot_t));
    slot.salt[0] ^= 0x01;
    KTEST_ASSERT(keyslot_unwrap(&slot, PASS_A, out) == E_ACCES,
                 "[STRICT] [KEYSLOT] nor one with an edited salt, which the tag covers");

    ft_memcpy(&slot, good, sizeof(keyslot_t));
    slot.iv[0] ^= 0x01;
    KTEST_ASSERT(keyslot_unwrap(&slot, PASS_A, out) == E_ACCES,
                 "[STRICT] [KEYSLOT] nor one with an edited IV");

    ft_memcpy(&slot, good, sizeof(keyslot_t));
    slot.wrapped[0] ^= 0x01;
    KTEST_ASSERT(keyslot_unwrap(&slot, PASS_A, out) == E_ACCES,
                 "[STRICT] [KEYSLOT] nor one with an edited wrapped key");

    /*
     * The iteration count is read off the disk, so it is attacker-controlled the
     * moment anything can write sector 0. Rejected before PBKDF2 is entered, on
     * the strength of pbkdf2_iterations_are_acceptable() - which exists for this
     * shape of input and had no caller reading a count off a disk until now.
     */
    ft_memcpy(&slot, good, sizeof(keyslot_t));
    slot.iterations = PBKDF2_MAX_ITERATIONS + 1;
    KTEST_ASSERT(keyslot_unwrap(&slot, PASS_A, out) == E_INVAL,
                 "[STRICT] [KEYSLOT] an absurd work factor is refused rather than spent");

    ft_memcpy(&slot, good, sizeof(keyslot_t));
    slot.iterations = 1;
    KTEST_ASSERT(keyslot_unwrap(&slot, PASS_A, out) == E_INVAL,
                 "[STRICT] [KEYSLOT] and so is one weaker than this build writes");
}

/**
 * @brief Tests the whole point of the design: the passphrase moves, the key does not.
 *
 * This is what makes a passphrase change one sector instead of a pass over every
 * file on the disk. If the second slot opened to a different key, changing a
 * passphrase would silently make the file system unreadable - and it would do it
 * at the moment the user was most sure they had just secured something.
 */
static void run_rewrap_assertions(const uint8_t original_dek[KEYSLOT_KEY_LEN]) {
    keyslot_t second;
    uint8_t out[KEYSLOT_KEY_LEN];

    KTEST_ASSERT(keyslot_create(&second, PASS_B, original_dek) == E_OK,
                 "[KEYSLOT] the same data key can be wrapped under a second passphrase");

    ft_memset(out, 0, sizeof(out));
    KTEST_ASSERT(keyslot_unwrap(&second, PASS_B, out) == E_OK &&
                 bytes_equal(out, original_dek, KEYSLOT_KEY_LEN),
                 "[STRICT] [KEYSLOT] and the new passphrase recovers the original key unchanged");

    KTEST_ASSERT(keyslot_unwrap(&second, PASS_A, out) == E_ACCES,
                 "[STRICT] [KEYSLOT] while the old passphrase no longer opens the new slot");
}

/**
 * @brief Tests the passphrase key slot.
 *
 * Expected Behavior:
 * - A slot created with a passphrase opens with it and yields the key it wrapped.
 * - A wrong passphrase is refused and writes nothing to the caller's buffer.
 * - Editing any field of a slot stops it opening.
 * - Wrapping the same key under a second passphrase preserves it exactly.
 *
 * Edge Cases Covered:
 * - Empty, null and over-long passphrases.
 * - Iteration counts above and below what this build accepts.
 * - A zeroed slot, which is what an unformatted superblock looks like.
 */
void run_keyslot_tests(void) {
    keyslot_t slot;
    keyslot_t empty;
    uint8_t dek[KEYSLOT_KEY_LEN];
    uint8_t out[KEYSLOT_KEY_LEN];

    printk("\n--- Disk Key Slot Tests ---\n");

    run_layout_assertions();

    ft_memset(&empty, 0, sizeof(empty));
    KTEST_ASSERT(keyslot_is_present(&empty) == 0,
                 "[STRICT] [KEYSLOT] a zeroed slot is not a slot");

    fill_test_dek(dek);
    KTEST_ASSERT(keyslot_create(&slot, PASS_A, dek) == E_OK,
                 "[KEYSLOT] a slot is created from a passphrase and a data key");
    KTEST_ASSERT(keyslot_is_present(&slot) == 1,
                 "[STRICT] [KEYSLOT] and reports itself present");
    KTEST_ASSERT(slot.iterations == PBKDF2_DEFAULT_ITERATIONS,
                 "[KEYSLOT] recording the work factor this build uses");

    /*
     * The wrapped key must not be the key. Obvious, and worth asserting: a wrap
     * that silently did nothing would pass every round-trip test in this file.
     */
    KTEST_ASSERT(!bytes_equal(slot.wrapped, dek, KEYSLOT_KEY_LEN),
                 "[STRICT] [KEYSLOT] the stored key is ciphertext, not the key itself");

    ft_memset(out, 0, sizeof(out));
    KTEST_ASSERT(keyslot_unwrap(&slot, PASS_A, out) == E_OK &&
                 bytes_equal(out, dek, KEYSLOT_KEY_LEN),
                 "[STRICT] [KEYSLOT] the right passphrase recovers the data key exactly");

    /*
     * The buffer is filled with a marker first, so that "unchanged" is something
     * this can actually observe rather than something it assumes.
     */
    ft_memset(out, 0x5A, sizeof(out));
    KTEST_ASSERT(keyslot_unwrap(&slot, PASS_B, out) == E_ACCES,
                 "[STRICT] [KEYSLOT] a wrong passphrase is refused");

    {
        int untouched = 1;
        for (uint32_t i = 0; i < KEYSLOT_KEY_LEN; i++) {
            if (out[i] != 0x5A) { untouched = 0; break; }
        }
        KTEST_ASSERT(untouched,
                     "[STRICT] [KEYSLOT] and hands back nothing at all, not a wrong key");
    }

    run_refusal_assertions(&slot);
    run_tamper_assertions(&slot);
    run_rewrap_assertions(dek);
}
