/*
 * File: keyslot.c
 * Purpose: Passphrase-protected wrapping of the file system's data key.
 *
 * This file is part of the esdumanOS test suite.
 *
 * Pure with respect to the disk: nothing here reads or writes a sector. The slot
 * arrives as a struct and leaves as a struct, and fs/vfs.c is what puts it in
 * the superblock. That separation is what lets the whole of this file be tested
 * without a formatted disk - see tests/kernel/test_keyslot.c, which exercises
 * every path in it including the ones a working system never takes.
 */
#include "types.h"
#include "errno.h"
#include "libft.h"
#include "keyslot.h"
#include "crypto.h"
#include "hmac.h"
#include "aes.h"
#include "pbkdf2.h"
#include "entropy.h"

/**
 * @brief The bytes the tag is computed over: salt || iv || wrapped.
 *
 * Everything in the slot except the tag itself. Binding the parameters and not
 * just the ciphertext is the point: with a tag over the wrapped key alone, an
 * attacker could keep a captured ciphertext and substitute their own salt, and
 * the unwrap would then succeed under a passphrase they chose.
 */
#define KEYSLOT_TAG_INPUT_LEN (KEYSLOT_SALT_LEN + KEYSLOT_IV_LEN + KEYSLOT_KEY_LEN)

/**
 * @brief Lays the tag input out contiguously.
 *
 * hmac_sha256() takes one buffer, and the three fields are not adjacent in a way
 * the compiler is allowed to promise even in a packed struct - reading them as
 * one run would be assuming a layout rather than stating it.
 */
static void keyslot_tag_input(const keyslot_t *slot, uint8_t out[KEYSLOT_TAG_INPUT_LEN]) {
    ft_memcpy(out, slot->salt, KEYSLOT_SALT_LEN);
    ft_memcpy(out + KEYSLOT_SALT_LEN, slot->iv, KEYSLOT_IV_LEN);
    ft_memcpy(out + KEYSLOT_SALT_LEN + KEYSLOT_IV_LEN, slot->wrapped, KEYSLOT_KEY_LEN);
}

/**
 * @brief Derives the key-encryption key for a slot.
 *
 * The iteration count comes from the slot rather than from this build, so a disk
 * written when the cost was lower still opens. The caller is responsible for
 * having checked it against pbkdf2_iterations_are_acceptable() first - a count
 * read off a disk is attacker-controlled, and PBKDF2 will happily spend an
 * afternoon on two billion rounds.
 */
static void keyslot_derive_kek(const keyslot_t *slot, const char *passphrase,
                               uint8_t kek_out[KEYSLOT_KEY_LEN]) {
    pbkdf2_hmac_sha256((const uint8_t *)passphrase, ft_strlen(passphrase),
                       slot->salt, KEYSLOT_SALT_LEN,
                       slot->iterations,
                       kek_out, KEYSLOT_KEY_LEN);
}

/**
 * @brief Overwrites key material that is about to go out of scope.
 *
 * volatile so that the compiler cannot decide the writes are unobservable and
 * drop them, which is exactly what it is entitled to do to a local buffer whose
 * last read has already happened.
 */
static void keyslot_wipe(uint8_t *buf, uint32_t len) {
    volatile uint8_t *p = buf;
    for (uint32_t i = 0; i < len; i++) p[i] = 0;
}

/**
 * @brief Whether a passphrase is one this code will accept.
 *
 * Empty is refused because an empty passphrase is not a decision a user can be
 * assumed to have made deliberately, and because the prompt that collects one
 * returns an empty string for "the user just pressed Enter".
 */
static int passphrase_is_usable(const char *passphrase) {
    uint32_t len;

    if (passphrase == 0) return 0;
    len = ft_strlen(passphrase);
    return len > 0 && len < KEYSLOT_MAX_PASSPHRASE;
}

/**
 * @brief Function keyslot_create
 */
int keyslot_create(keyslot_t *slot, const char *passphrase, const uint8_t dek[KEYSLOT_KEY_LEN]) {
    uint8_t kek[KEYSLOT_KEY_LEN];
    uint8_t tag_input[KEYSLOT_TAG_INPUT_LEN];
    struct AES_ctx ctx;

    if (slot == 0 || dek == 0 || !passphrase_is_usable(passphrase)) return E_INVAL;

    /*
     * A fresh salt and IV every time, including when only the passphrase is
     * changing. Reusing the salt would let somebody who kept an old copy of the
     * superblock tell that the new passphrase is the same as the old one, by
     * deriving once and comparing tags.
     */
    if (generate_random_bytes(slot->salt, KEYSLOT_SALT_LEN) == ENTROPY_FAIL) return E_IO;
    if (generate_random_bytes(slot->iv, KEYSLOT_IV_LEN) == ENTROPY_FAIL) return E_IO;

    slot->iterations = PBKDF2_DEFAULT_ITERATIONS;

    keyslot_derive_kek(slot, passphrase, kek);

    /*
     * Exactly two AES blocks, so there is no padding and no length to record.
     * AES_CBC_encrypt_buffer() advances the context's IV as it goes, which is
     * why the slot's copy is written before this rather than read after it.
     */
    ft_memcpy(slot->wrapped, dek, KEYSLOT_KEY_LEN);
    AES_init_ctx_iv(&ctx, kek, slot->iv);
    AES_CBC_encrypt_buffer(&ctx, slot->wrapped, KEYSLOT_KEY_LEN);

    keyslot_tag_input(slot, tag_input);
    hmac_sha256(kek, KEYSLOT_KEY_LEN, tag_input, KEYSLOT_TAG_INPUT_LEN, slot->tag);

    keyslot_wipe(kek, sizeof(kek));
    keyslot_wipe((uint8_t *)&ctx, sizeof(ctx));
    return E_OK;
}

/**
 * @brief Function keyslot_unwrap
 */
int keyslot_unwrap(const keyslot_t *slot, const char *passphrase, uint8_t dek_out[KEYSLOT_KEY_LEN]) {
    uint8_t kek[KEYSLOT_KEY_LEN];
    uint8_t tag_input[KEYSLOT_TAG_INPUT_LEN];
    uint8_t expected_tag[KEYSLOT_TAG_LEN];
    uint8_t plain[KEYSLOT_KEY_LEN];
    struct AES_ctx ctx;
    int mismatch;

    if (slot == 0 || dek_out == 0 || !passphrase_is_usable(passphrase)) return E_INVAL;

    /*
     * Before spending any time on it. The count is on the disk, so it is under
     * the control of anybody who can write the disk, and the ceiling in
     * pbkdf2.h exists for precisely this shape of input.
     */
    if (!pbkdf2_iterations_are_acceptable(slot->iterations)) return E_INVAL;

    keyslot_derive_kek(slot, passphrase, kek);

    keyslot_tag_input(slot, tag_input);
    hmac_sha256(kek, KEYSLOT_KEY_LEN, tag_input, KEYSLOT_TAG_INPUT_LEN, expected_tag);

    mismatch = crypto_ct_cmp_bytes(expected_tag, slot->tag, KEYSLOT_TAG_LEN);

    /*
     * The key is unwrapped only after the tag has been accepted. Decrypting
     * first and checking afterwards would hand a caller 32 bytes derived from a
     * guessed passphrase, and a caller that forgot to look at the return value
     * would encrypt the disk under them.
     */
    if (mismatch != 0) {
        keyslot_wipe(kek, sizeof(kek));
        keyslot_wipe(expected_tag, sizeof(expected_tag));
        return E_ACCES;
    }

    ft_memcpy(plain, slot->wrapped, KEYSLOT_KEY_LEN);
    AES_init_ctx_iv(&ctx, kek, slot->iv);
    AES_CBC_decrypt_buffer(&ctx, plain, KEYSLOT_KEY_LEN);
    ft_memcpy(dek_out, plain, KEYSLOT_KEY_LEN);

    keyslot_wipe(plain, sizeof(plain));
    keyslot_wipe(kek, sizeof(kek));
    keyslot_wipe(expected_tag, sizeof(expected_tag));
    keyslot_wipe((uint8_t *)&ctx, sizeof(ctx));
    return E_OK;
}

/**
 * @brief Function keyslot_is_present
 */
int keyslot_is_present(const keyslot_t *slot) {
    if (slot == 0) return 0;

    /*
     * The iteration count decides, not the tag or the ciphertext. Those are
     * indistinguishable from random and a zeroed one is a value they could in
     * principle take; a slot that was never written has an iteration count of
     * zero, and zero is a count no build of this kernel will ever produce.
     */
    return slot->iterations != 0;
}
