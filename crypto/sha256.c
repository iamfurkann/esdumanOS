/**
 * @file sha256.c
 * @brief Implementation of the SHA256 hash function
 *
 * Both the one-shot helpers and HMAC are built on one incremental context, so
 * the padding and length encoding exist in a single place. The incremental form
 * is what lets HMAC hash "key_pad || message" without first assembling the two
 * into a contiguous buffer - which is what used to force an allocation on every
 * single call, and therefore on every one of PBKDF2's iterations.
 */
#include "types.h"
#include "crypto.h"

#define ROTRIGHT(word,bits) (((word) >> (bits)) | ((word) << (32-(bits))))
#define CH(x,y,z) (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x,y,z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x) (ROTRIGHT(x,2) ^ ROTRIGHT(x,13) ^ ROTRIGHT(x,22))
#define EP1(x) (ROTRIGHT(x,6) ^ ROTRIGHT(x,11) ^ ROTRIGHT(x,25))
#define SIG0(x) (ROTRIGHT(x,7) ^ ROTRIGHT(x,18) ^ ((x) >> 3))
#define SIG1(x) (ROTRIGHT(x,17) ^ ROTRIGHT(x,19) ^ ((x) >> 10))

static const uint32_t k[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

static void sha256_transform(uint32_t state[8], const uint8_t data[64]) {
    uint32_t m[64];
    for (int i = 0, j = 0; i < 16; ++i, j += 4) {
        m[i] = (data[j] << 24) | (data[j + 1] << 16) | (data[j + 2] << 8) | (data[j + 3]);
    }
    for (int i = 16; i < 64; ++i) {
        m[i] = SIG1(m[i - 2]) + m[i - 7] + SIG0(m[i - 15]) + m[i - 16];
    }

    uint32_t a = state[0], b = state[1], c = state[2], d = state[3],
             e = state[4], f = state[5], g = state[6], h = state[7];

    for (int i = 0; i < 64; ++i) {
        uint32_t t1 = h + EP1(e) + CH(e,f,g) + k[i] + m[i];
        uint32_t t2 = EP0(a) + MAJ(a,b,c);
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }
    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

/**
 * @brief Starts a new incremental SHA-256 computation.
 * @param ctx Context to initialise.
 */
void sha256_init(sha256_ctx_t *ctx) {
    ctx->state[0] = 0x6a09e667; ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372; ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f; ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab; ctx->state[7] = 0x5be0cd19;
    ctx->total_len = 0;
    ctx->buffer_len = 0;
}

/**
 * @brief Feeds more data into a SHA-256 computation.
 *
 * @param ctx  Context previously passed to sha256_init().
 * @param data Bytes to absorb; ignored when len is 0.
 * @param len  Number of bytes.
 */
void sha256_update(sha256_ctx_t *ctx, const uint8_t *data, uint32_t len) {
    if (data == 0 || len == 0) return;

    ctx->total_len += len;

    while (len > 0) {
        uint32_t space = 64 - ctx->buffer_len;
        uint32_t take = (len < space) ? len : space;

        for (uint32_t i = 0; i < take; i++) {
            ctx->buffer[ctx->buffer_len + i] = data[i];
        }
        ctx->buffer_len += take;
        data += take;
        len  -= take;

        if (ctx->buffer_len == 64) {
            sha256_transform(ctx->state, ctx->buffer);
            ctx->buffer_len = 0;
        }
    }
}

/**
 * @brief Appends the padding and produces the digest.
 *
 * @param ctx Context to finalise; not reusable afterwards without sha256_init().
 * @param output_binary 32-byte output buffer.
 */
void sha256_final(sha256_ctx_t *ctx, uint8_t *output_binary) {
    uint32_t rem = ctx->buffer_len;

    ctx->buffer[rem++] = 0x80;

    if (rem > 56) {
        while (rem < 64) ctx->buffer[rem++] = 0x00;
        sha256_transform(ctx->state, ctx->buffer);
        rem = 0;
    }
    while (rem < 56) ctx->buffer[rem++] = 0x00;

    uint32_t bitlen_hi = (ctx->total_len >> 29);
    uint32_t bitlen_lo = (ctx->total_len << 3);

    ctx->buffer[56] = (bitlen_hi >> 24) & 0xFF;
    ctx->buffer[57] = (bitlen_hi >> 16) & 0xFF;
    ctx->buffer[58] = (bitlen_hi >> 8) & 0xFF;
    ctx->buffer[59] = (bitlen_hi) & 0xFF;
    ctx->buffer[60] = (bitlen_lo >> 24) & 0xFF;
    ctx->buffer[61] = (bitlen_lo >> 16) & 0xFF;
    ctx->buffer[62] = (bitlen_lo >> 8) & 0xFF;
    ctx->buffer[63] = (bitlen_lo) & 0xFF;

    sha256_transform(ctx->state, ctx->buffer);

    for (int i = 0; i < 8; i++) {
        output_binary[(i*4)]     = (ctx->state[i] >> 24) & 0xFF;
        output_binary[(i*4) + 1] = (ctx->state[i] >> 16) & 0xFF;
        output_binary[(i*4) + 2] = (ctx->state[i] >> 8)  & 0xFF;
        output_binary[(i*4) + 3] = (ctx->state[i])       & 0xFF;
    }
}

/**
 * @brief Compute SHA256 hash and return as binary data
 * @param input Pointer to the input data
 * @param len Length of the input data in bytes
 * @param output_binary Pointer to the 32-byte output buffer
 * @return None
 */
void sha256_binary(const uint8_t *input, uint32_t len, uint8_t *output_binary) {
    sha256_ctx_t ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, input, len);
    sha256_final(&ctx, output_binary);
}

/**
 * @brief Compute SHA256 hash and return as hex string
 * @param input Pointer to the null-terminated input string
 * @param output_hex Pointer to the output buffer for the hex string
 * @return None
 */
void sha256_to_hex(const char *input, char *output_hex) {
    uint32_t len = 0;
    while (input[len]) len++;

    uint8_t digest[32];
    sha256_binary((const uint8_t *)input, len, digest);

    const char hex_chars[] = "0123456789abcdef";
    for (int i = 0; i < 32; i++) {
        output_hex[i * 2]     = hex_chars[digest[i] >> 4];
        output_hex[i * 2 + 1] = hex_chars[digest[i] & 0x0F];
    }
    output_hex[64] = '\0';
}
