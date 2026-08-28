/*
 * File: crypto_fs.c
 * Purpose: Cryptographic file system functions using AES and SHA-256.
 *
 * This file is part of the esdumanOS test suite.
 */
#include "aes.h"
#include "fs.h"
#include "crypto.h"
#include "security.h"
#include "errno.h"
#include "entropy.h"
#include "stdio.h"
#include "kheap.h"
#include "hmac.h"
#include "bcache.h"
#include "klog.h"


/**
 * @brief Derives the initialisation vector for one encrypted file.
 *
 * CBC needs an IV that is unique per (key, message) and, to resist chosen-plaintext
 * attacks, unpredictable to anyone who does not hold the key. Taking it straight
 * from generate_random_bytes() met neither requirement when RDRAND was absent:
 * the pool was worth perhaps twenty bits, and nothing at all guaranteed two files
 * would not receive the same IV.
 *
 * Deriving it instead splits the two requirements apart. The monotonic counter
 * makes the HMAC input distinct on every single call, so uniqueness holds even
 * with a completely dead entropy pool; keying the derivation with the file key
 * makes the result unpredictable to anyone without that key. Pool bytes are still
 * mixed in, so with RDRAND present the IV is fully random as well - but nothing
 * now depends on that.
 *
 * The wire format is unchanged: the IV is still the first 16 plaintext bytes of
 * the file, so fs_read_encrypted() needs no matching change and files written by
 * earlier builds still decrypt.
 *
 * BOUNDARY: the counter restarts at zero on every boot, so what keeps boot N's
 * first file from sharing an IV with boot N+1's is the pool bytes, not the
 * counter - and those carry the RTC timestamp and TSC that entropy_init() mixed
 * in. Two cold boots of the same image inside the same RTC second are therefore
 * the one case this does not provably separate. See the note in entropy.h.
 *
 * @param key    The 32-byte file key; also the HMAC key.
 * @param iv_out 16-byte output.
 * @return E_OK, or E_IO when the entropy pool refuses to serve.
 */
static int derive_file_iv(const uint8_t key[32], uint8_t iv_out[16]) {
    uint8_t pool_bytes[16];
    int quality = generate_random_bytes(pool_bytes, sizeof(pool_bytes));

    /*
     * ENTROPY_FAIL means the pool is not initialised, so not even the uniqueness
     * of the counter can be trusted to have been seeded. Refuse rather than write
     * a file whose IV might repeat. ENTROPY_WEAK is fine here by construction.
     */
    if (quality == ENTROPY_FAIL) return E_IO;

    uint64_t counter = entropy_next_counter();

    static const char label[] = "esdumanOS-iv-v1";
    uint8_t material[sizeof(label) + sizeof(counter) + sizeof(pool_bytes)];
    uint32_t pos = 0;

    for (uint32_t i = 0; i < sizeof(label); i++)       material[pos++] = (uint8_t)label[i];
    for (uint32_t i = 0; i < sizeof(counter); i++)     material[pos++] = (uint8_t)(counter >> (i * 8));
    for (uint32_t i = 0; i < sizeof(pool_bytes); i++)  material[pos++] = pool_bytes[i];

    uint8_t mac[32];
    hmac_sha256(key, 32, material, pos, mac);

    for (int i = 0; i < 16; i++) iv_out[i] = mac[i];

    /* Do not leave key-adjacent material on the stack. */
    for (uint32_t i = 0; i < sizeof(material); i++) material[i] = 0;
    for (uint32_t i = 0; i < sizeof(mac); i++) mac[i] = 0;
    for (uint32_t i = 0; i < sizeof(pool_bytes); i++) pool_bytes[i] = 0;

    return E_OK;
}

/**
 * @brief Create an encrypted file on the file system.
 * @param name The name of the file.
 * @param data The plaintext data to write.
 * @param len Length of the data.
 * @param key The 32-byte AES key.
 * @param parent_id The ID of the parent directory.
 * @return Status code (e.g., E_OK or error).
 */
int fs_create_encrypted(const char *name, const uint8_t *data, uint32_t len, const uint8_t key[32], fs_id_t parent_id) {
    uint32_t payload_len = 40 + len;
    uint32_t padded_len = (payload_len + 15) & ~15;
    uint32_t total_len = 16 + padded_len;

    uint8_t *enc_buffer = (uint8_t *)kmalloc(total_len);
    if (!enc_buffer) return E_NOMEM;

    int iv_res = derive_file_iv(key, enc_buffer);
    if (iv_res != E_OK) {
        kfree(enc_buffer);
        return iv_res;
    }

    /*
     * A djb2 checksum used to be computed over the plaintext here and then never
     * read. The HMAC-SHA256 tag written into the header below is what actually
     * detects tampering, and unlike a checksum it is keyed - so the weaker
     * unused one was only costing a pass over the data.
     */
    uint32_t *hdr = (uint32_t *)(enc_buffer + 16);
    hdr[0] = 0x53414645; // "SAFE"
    hdr[1] = len;        
    hmac_sha256(key, 32, data, len, (uint8_t *)&hdr[2]);

    for(uint32_t i = 0; i < len; i++) {
        enc_buffer[56 + i] = data[i];
    }
    
    for(uint32_t i = 40 + len; i < padded_len; i++) {
        enc_buffer[16 + i] = 0;
    }
    
    struct AES_ctx ctx;
    AES_init_ctx_iv(&ctx, key, enc_buffer); 
    AES_CBC_encrypt_buffer(&ctx, enc_buffer + 16, padded_len); 

    int ret = fs_create_file_raw(name, enc_buffer, total_len, parent_id);
    kfree(enc_buffer);
    return ret;
}

/**
 * @brief Reports how many plaintext bytes an encrypted file holds.
 *
 * The directory table records what the file occupies on disk, and for an
 * encrypted file that is 16 bytes of IV plus a 40-byte header plus the plaintext
 * padded up to an AES block - so it is never the number read() will hand back.
 * Since SEC_LEVEL_CRYPTO_ENFORCED is the default (see security.c), that gap is
 * the normal case rather than a corner one, which is why stat() and lseek() ask
 * here instead of reading dir_table directly.
 *
 * This costs one block, not one file. fs_create_encrypted() above writes the
 * magic and the original length as the first eight bytes of the payload, so both
 * land inside ciphertext block 0 and decrypting that single block is enough. The
 * IV sits in front of it in the clear, which is what CBC needs to decrypt block 0
 * on its own.
 *
 * BOUNDARY: the length returned here is NOT authenticated. The header is
 * encrypted, but the HMAC covers the plaintext and is only checked by
 * fs_read_encrypted() once it has the whole file. Someone who can write to the
 * disk can therefore make stat() report a wrong size - the subsequent read()
 * fails the integrity check, but the size itself carries no guarantee. Do not
 * treat a stat() size as evidence that the file is intact.
 *
 * @param file Open file whose first sector is to be inspected.
 * @param key 32-byte master key.
 * @param out_size Receives the plaintext length on success.
 * @return E_OK on success, or a negative error code.
 */
int fs_size_encrypted(vfs_file_t *file, const uint8_t key[32], uint32_t *out_size) {
    if (!file || !out_size) return E_INVAL;

    /*
     * Same threshold fs_read_encrypted() uses: a file that cannot even hold the
     * IV and header has no plaintext to report, and reads of it return 0.
     */
    if (file->file_size <= 56) {
        *out_size = 0;
        return E_OK;
    }

    /*
     * The IV and the first ciphertext block are the first 32 bytes of the file,
     * so they are always inside the first sector of its first cluster. Read it
     * through fs_read_sector(), which is where fs_read_raw() would find it
     * anyway and which is what adds the partition offset.
     */
    uint8_t sector[512];
    fs_read_sector(fs_cluster_to_sector(file->start_cluster), sector);

    /* Aligned because the header fields are read back through a uint32_t view. */
    uint8_t block[AES_BLOCKLEN] __attribute__((aligned(4)));
    for (int i = 0; i < AES_BLOCKLEN; i++) block[i] = sector[16 + i];

    struct AES_ctx ctx;
    AES_init_ctx_iv(&ctx, key, sector);
    AES_CBC_decrypt_buffer(&ctx, block, AES_BLOCKLEN);

    uint32_t magic = ((uint32_t *)block)[0];
    uint32_t orig_len = ((uint32_t *)block)[1];

    if (magic != 0x53414645) {
        /* Wrong key or a corrupted header. Reporting the on-disk size here would
         * be inventing an answer; fs_read_encrypted() refuses the same case. */
        return E_IO;
    }

    /*
     * Cross-check against what the file can physically hold, so a tampered
     * length cannot be handed back as though it were plausible. The payload is
     * everything after the IV; the header takes 40 bytes of it.
     */
    uint32_t max_possible_len = (file->file_size - 16) - 40;
    if (orig_len > max_possible_len) return E_IO;

    *out_size = orig_len;
    return E_OK;
}

/**
 * @brief Decrypts one stored blob in place and verifies it.
 *
 * The stored form is the same wherever it comes from: sixteen bytes of IV, then
 * AES-256-CBC over "SAFE" || original length || HMAC-SHA256 || the data. A file
 * read off the disk and a program that tools/encrypt_tool baked into the kernel
 * image are byte-for-byte the same shape, which is not a coincidence - the tool
 * was written to match fs_create_encrypted().
 *
 * So this is one function rather than two. It was the middle of
 * fs_read_encrypted() until v1.1.0 needed the identical steps for the embedded
 * programs, and a second copy of a format parser is a second place for the
 * format to be understood slightly differently.
 *
 * @param buf       The blob, decrypted in place.
 * @param len       Bytes in the blob.
 * @param key       The key it was encrypted under.
 * @param plain_out Receives a pointer into @p buf at the plaintext.
 * @return The plaintext length, or a negative errno.
 */
static int crypto_fs_open_blob(uint8_t *buf, uint32_t len, const uint8_t key[32],
                               uint8_t **plain_out) {
    struct AES_ctx ctx;
    uint32_t cipher_len;
    uint32_t *hdr;
    uint32_t orig_len;
    uint8_t *stored_hash;
    uint8_t *plaintext;
    uint8_t calc_hash[32];

    if (len <= 56) return E_INVAL;

    cipher_len = len - 16;

    AES_init_ctx_iv(&ctx, key, buf);
    AES_CBC_decrypt_buffer(&ctx, buf + 16, cipher_len);

    hdr = (uint32_t *)(buf + 16);
    orig_len = hdr[1];
    stored_hash = (uint8_t *)&hdr[2];

    if (hdr[0] != 0x53414645) return E_ACCES;
    if (orig_len > cipher_len - 40) return E_INVAL;

    plaintext = buf + 56;
    hmac_sha256(key, 32, plaintext, orig_len, calc_hash);

    /*
     * Constant-time, like every other comparison of a secret in this tree. It
     * was a plain loop with an early-exit flag until v1.1.0 gathered the one
     * implementation into crypto_ct_cmp_bytes(); the timing signal here is
     * weaker than the one on a password, because reaching it needs control of
     * the ciphertext, but "weaker" is not a reason to write the careless form.
     */
    if (crypto_ct_cmp_bytes(calc_hash, stored_hash, 32) != 0) return E_IO;

    *plain_out = plaintext;
    return (int)orig_len;
}

/**
 * @brief Function fs_install_image_asset
 *
 * Writes one of the programs embedded in the kernel image onto the disk, moving
 * it from the build-time key to this disk's own.
 *
 * The call sites used to be fs_create_file_raw(), which stored the blob exactly
 * as the build had encrypted it - so every /bin program on every disk was
 * readable with a key that ships inside the ISO. That was invisible while the
 * file system used the same key for everything, and it is the whole point of
 * this release that it no longer does: a disk whose programs open with a
 * published key is a disk with a published key on it.
 *
 * @param name      Name to create.
 * @param blob      The embedded, build-encrypted image.
 * @param blob_len  Its length.
 * @param parent_id Directory to create it in.
 * @return E_OK, or a negative errno.
 */
int fs_install_image_asset(const char *name, const uint8_t *blob, uint32_t blob_len,
                           fs_id_t parent_id) {
    uint8_t *work;
    uint8_t *plain = 0;
    int plain_len;
    int res;

    if (!elf_asset_key_available()) return E_NOSYS;

    work = (uint8_t *)kmalloc(blob_len);
    if (!work) return E_NOMEM;

    for (uint32_t i = 0; i < blob_len; i++) work[i] = blob[i];

    plain_len = crypto_fs_open_blob(work, blob_len, elf_asset_key, &plain);
    if (plain_len < 0) {
        klog(LOG_LEVEL_ERROR, "VFS", "An embedded program failed to decrypt; not installing it.");
        kfree(work);
        return plain_len;
    }

    /*
     * fs_create_file(), not fs_create_file_raw(): the point is to re-encrypt it
     * under the key that came out of the disk's own slot. At CRYPTO_ENFORCED,
     * which is the default, that is what fs_create_file() does.
     */
    res = fs_create_file(name, plain, (uint32_t)plain_len, parent_id);

    kfree(work);
    return res;
}

/**
 * @brief Read an encrypted file from the file system.
 * @param file The file entry pointer.
 * @param buffer Buffer to store the decrypted data.
 * @param size Number of bytes to read.
 * @param key The 32-byte AES key.
 * @return Number of bytes read.
 */
int fs_read_encrypted(vfs_file_t *file, uint8_t *buffer, uint32_t size, const uint8_t key[32]) {
    if (file->file_size <= 56) return 0;

    uint8_t *temp_buf = (uint8_t *)kmalloc(file->file_size);
    if (!temp_buf) return 0;

    uint32_t requested_offset = file->current_offset;
    file->current_offset = 0;

    int read_bytes = fs_read_raw(file, temp_buf, file->file_size);
    if (read_bytes <= 56) {
        file->current_offset = requested_offset;
        kfree(temp_buf);
        return 0;
    }

    uint8_t *plaintext = 0;
    int opened = crypto_fs_open_blob(temp_buf, (uint32_t)read_bytes, key, &plaintext);

    if (opened < 0) {
        if (opened == E_ACCES) {
            printk("[CRYPTO ERROR] Magic Number mismatch! Incorrect password or corrupted data.\n");
        } else if (opened == E_INVAL) {
            printk("[CRYPTO CRITICAL ERROR] File size specified in header is larger than data!\n");
        } else {
            terminal_setcolor(VGA_COLOR_WHITE, VGA_COLOR_RED);
            printk("\n[CRYPTO CRITICAL ERROR] INTEGRITY VIOLATION!\n");
            printk("File has been tampered with or SHA-256 Hash mismatch!\n");
            terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        }
        file->current_offset = requested_offset;
        kfree(temp_buf);
        return 0;
    }

    uint32_t orig_len = (uint32_t)opened;
    uint32_t bytes_to_copy = size;
    if (requested_offset >= orig_len) {
        bytes_to_copy = 0; 
    } else if (requested_offset + bytes_to_copy > orig_len) {
        bytes_to_copy = orig_len - requested_offset;
    }

    for(uint32_t i = 0; i < bytes_to_copy; i++) {
        buffer[i] = plaintext[requested_offset + i];
    }

    file->current_offset = requested_offset + bytes_to_copy;
    kfree(temp_buf);

    return bytes_to_copy;
}