/*
 * crypt(3) with SHA-512 password hashing ("$6$"), the format used in
 * /etc/shadow by most systems, so hashes can be generated on the host
 * (e.g. `openssl passwd -6' or `mkpasswd -m sha-512').
 *
 * Implements "Unix crypt using SHA-256 and SHA-512" by Ulrich Drepper,
 * https://www.akkadia.org/drepper/SHA-crypt.txt
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * SHA-512 (FIPS 180-4)
 */

struct sha512 {
    uint64_t state[8];
    uint64_t len;           /* message length in bytes */
    uint8_t  buf[128];
    size_t   used;
};

static const uint64_t K[80] = {
    0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL,
    0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL, 0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL,
    0xd807aa98a3030242ULL, 0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
    0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL, 0xc19bf174cf692694ULL,
    0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL, 0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
    0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
    0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL,
    0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL, 0x06ca6351e003826fULL, 0x142929670a0e6e70ULL,
    0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
    0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
    0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL, 0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL,
    0xd192e819d6ef5218ULL, 0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
    0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL,
    0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL, 0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL,
    0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
    0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL,
    0xca273eceea26619cULL, 0xd186b8c721c0c207ULL, 0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL,
    0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
    0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL, 0x431d67c49c100d4cULL,
    0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL, 0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL,
};

#define ROR(x, n)   (((x) >> (n)) | ((x) << (64 - (n))))

static void sha512_block(struct sha512 *ctx, const uint8_t *p)
{
    uint64_t w[80], a, b, c, d, e, f, g, h;

    for (int i = 0; i < 16; ++i) {
        w[i] = 0;
        for (int j = 0; j < 8; ++j)
            w[i] = (w[i] << 8) | p[i * 8 + j];
    }

    for (int i = 16; i < 80; ++i) {
        uint64_t s0 = ROR(w[i-15], 1) ^ ROR(w[i-15], 8) ^ (w[i-15] >> 7);
        uint64_t s1 = ROR(w[i-2], 19) ^ ROR(w[i-2], 61) ^ (w[i-2] >> 6);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }

    a = ctx->state[0]; b = ctx->state[1]; c = ctx->state[2]; d = ctx->state[3];
    e = ctx->state[4]; f = ctx->state[5]; g = ctx->state[6]; h = ctx->state[7];

    for (int i = 0; i < 80; ++i) {
        uint64_t t1 = h + (ROR(e, 14) ^ ROR(e, 18) ^ ROR(e, 41)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
        uint64_t t2 = (ROR(a, 28) ^ ROR(a, 34) ^ ROR(a, 39)) + ((a & b) ^ (a & c) ^ (b & c));
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f; ctx->state[6] += g; ctx->state[7] += h;
}

static void sha512_init(struct sha512 *ctx)
{
    static const uint64_t iv[8] = {
        0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
        0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL, 0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL,
    };

    memcpy(ctx->state, iv, sizeof(iv));
    ctx->len  = 0;
    ctx->used = 0;
}

static void sha512_update(struct sha512 *ctx, const void *data, size_t len)
{
    const uint8_t *p = data;

    ctx->len += len;

    while (len) {
        size_t n = sizeof(ctx->buf) - ctx->used;
        if (n > len)
            n = len;

        memcpy(ctx->buf + ctx->used, p, n);
        ctx->used += n;
        p   += n;
        len -= n;

        if (ctx->used == sizeof(ctx->buf)) {
            sha512_block(ctx, ctx->buf);
            ctx->used = 0;
        }
    }
}

static void sha512_final(struct sha512 *ctx, uint8_t out[64])
{
    uint64_t bits = ctx->len * 8;

    /* 0x80, zero padding, then the 128-bit big-endian bit length */
    ctx->buf[ctx->used++] = 0x80;

    if (ctx->used > 112) {
        memset(ctx->buf + ctx->used, 0, sizeof(ctx->buf) - ctx->used);
        sha512_block(ctx, ctx->buf);
        ctx->used = 0;
    }

    memset(ctx->buf + ctx->used, 0, 120 - ctx->used);
    for (int i = 0; i < 8; ++i)
        ctx->buf[120 + i] = bits >> (56 - 8 * i);

    sha512_block(ctx, ctx->buf);

    for (int i = 0; i < 64; ++i)
        out[i] = ctx->state[i / 8] >> (56 - 8 * (i % 8));
}

/*
 * SHA-512 crypt
 */

#define SALT_MAX        16
#define ROUNDS_DEFAULT  5000
#define ROUNDS_MIN      1000
#define ROUNDS_MAX      999999999

static const char b64[] =
    "./0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

static char *b64_from_24bit(char *out, uint8_t b2, uint8_t b1, uint8_t b0, int n)
{
    uint32_t w = (b2 << 16) | (b1 << 8) | b0;

    while (n--) {
        *out++ = b64[w & 0x3f];
        w >>= 6;
    }

    return out;
}

/* add len bytes of the 64-byte digest repeated as needed */
static void add_repeated(struct sha512 *ctx, const uint8_t digest[64], size_t len)
{
    for (; len >= 64; len -= 64)
        sha512_update(ctx, digest, 64);

    sha512_update(ctx, digest, len);
}

static char *sha512_crypt(const char *key, const char *setting, char *out, size_t size)
{
    static const char prefix[] = "$6$";
    static const char rounds_prefix[] = "rounds=";

    struct sha512 ctx, alt;
    uint8_t a[64], b[64], dp[64], ds[64];
    unsigned long rounds = ROUNDS_DEFAULT;
    int custom_rounds = 0;

    const char *salt = setting + sizeof(prefix) - 1;

    if (!strncmp(salt, rounds_prefix, sizeof(rounds_prefix) - 1)) {
        char *end;
        unsigned long r = strtoul(salt + sizeof(rounds_prefix) - 1, &end, 10);

        if (*end != '$')
            return NULL;

        salt = end + 1;
        rounds = r < ROUNDS_MIN ? ROUNDS_MIN : r > ROUNDS_MAX ? ROUNDS_MAX : r;
        custom_rounds = 1;
    }

    size_t salt_len = strcspn(salt, "$");
    if (salt_len > SALT_MAX)
        salt_len = SALT_MAX;

    size_t key_len = strlen(key);

    /* B = SHA512(key salt key) */
    sha512_init(&alt);
    sha512_update(&alt, key, key_len);
    sha512_update(&alt, salt, salt_len);
    sha512_update(&alt, key, key_len);
    sha512_final(&alt, b);

    /* A = SHA512(key salt B... per key bit) */
    sha512_init(&ctx);
    sha512_update(&ctx, key, key_len);
    sha512_update(&ctx, salt, salt_len);
    add_repeated(&ctx, b, key_len);

    for (size_t n = key_len; n; n >>= 1) {
        if (n & 1)
            sha512_update(&ctx, b, 64);
        else
            sha512_update(&ctx, key, key_len);
    }

    sha512_final(&ctx, a);

    /* DP = SHA512(key repeated key_len times), P = DP repeated to key_len */
    sha512_init(&alt);
    for (size_t i = 0; i < key_len; ++i)
        sha512_update(&alt, key, key_len);
    sha512_final(&alt, dp);

    /* DS = SHA512(salt repeated 16 + A[0] times), S = DS repeated to salt_len */
    sha512_init(&alt);
    for (size_t i = 0; i < 16 + (size_t) a[0]; ++i)
        sha512_update(&alt, salt, salt_len);
    sha512_final(&alt, ds);

    for (unsigned long i = 0; i < rounds; ++i) {
        sha512_init(&ctx);

        if (i & 1)
            add_repeated(&ctx, dp, key_len);
        else
            sha512_update(&ctx, a, 64);

        if (i % 3)
            add_repeated(&ctx, ds, salt_len);

        if (i % 7)
            add_repeated(&ctx, dp, key_len);

        if (i & 1)
            sha512_update(&ctx, a, 64);
        else
            add_repeated(&ctx, dp, key_len);

        sha512_final(&ctx, a);
    }

    /* "$6$" ["rounds=N$"] salt "$" + 86 characters */
    int n = custom_rounds ?
        snprintf(out, size, "%srounds=%lu$%.*s$", prefix, rounds, (int) salt_len, salt) :
        snprintf(out, size, "%s%.*s$", prefix, (int) salt_len, salt);

    if (n < 0 || (size_t) n + 86 + 1 > size)
        return NULL;

    static const uint8_t order[21][3] = {
        { 0, 21, 42}, {22, 43,  1}, {44,  2, 23}, { 3, 24, 45}, {25, 46,  4},
        {47,  5, 26}, { 6, 27, 48}, {28, 49,  7}, {50,  8, 29}, { 9, 30, 51},
        {31, 52, 10}, {53, 11, 32}, {12, 33, 54}, {34, 55, 13}, {56, 14, 35},
        {15, 36, 57}, {37, 58, 16}, {59, 17, 38}, {18, 39, 60}, {40, 61, 19},
        {62, 20, 41},
    };

    char *p = out + n;

    for (int i = 0; i < 21; ++i)
        p = b64_from_24bit(p, a[order[i][0]], a[order[i][1]], a[order[i][2]], 4);

    p = b64_from_24bit(p, 0, 0, a[63], 2);
    *p = '\0';

    /* don't leave intermediate values around */
    memset(&ctx, 0, sizeof(ctx));
    memset(&alt, 0, sizeof(alt));
    memset(b, 0, sizeof(b));
    memset(dp, 0, sizeof(dp));
    memset(ds, 0, sizeof(ds));

    return out;
}

char *crypt(const char *key, const char *setting)
{
    /* "$6$rounds=999999999$" + 16 salt + "$" + 86 + NUL */
    static char buf[128];

    if (strncmp(setting, "$6$", 3)) {
        /* only SHA-512 crypt is supported */
        errno = EINVAL;
        return NULL;
    }

    return sha512_crypt(key, setting, buf, sizeof(buf));
}
