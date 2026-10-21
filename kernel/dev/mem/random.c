/*
 *          Random numbers
 *
 * ChaCha20 as a generator: its key is made from a pool that the cycle
 * counter is stirred into on every timer tick (and at each request), and
 * replaced after each request with keystream nobody saw. There is no
 * hardware source, the randomness is that of the timing of interrupts
 * and requests: unpredictable enough for hash seeds and arc4random(),
 * not a strong key generator. /dev/random and /dev/urandom are the same.
 */

#include <core/system.h>
#include <core/string.h>
#include <core/arch.h>
#include <dev/dev.h>
#include <dev/random.h>
#include <fs/posix.h>

#include <memdev.h>

#define ROTL(a, b) (((a) << (b)) | ((a) >> (32 - (b))))
#define QR(a, b, c, d) (                         \
    a += b, d ^= a, d = ROTL(d, 16),             \
    c += d, b ^= c, b = ROTL(b, 12),             \
    a += b, d ^= a, d = ROTL(d, 8),              \
    c += d, b ^= c, b = ROTL(b, 7))

static void chacha20_block(const uint32_t in[16], uint32_t out[16])
{
    uint32_t x[16];

    for (int i = 0; i < 16; ++i)
        x[i] = in[i];

    for (int i = 0; i < 10; ++i) {
        QR(x[0], x[4], x[ 8], x[12]);
        QR(x[1], x[5], x[ 9], x[13]);
        QR(x[2], x[6], x[10], x[14]);
        QR(x[3], x[7], x[11], x[15]);
        QR(x[0], x[5], x[10], x[15]);
        QR(x[1], x[6], x[11], x[12]);
        QR(x[2], x[7], x[ 8], x[13]);
        QR(x[3], x[4], x[ 9], x[14]);
    }

    for (int i = 0; i < 16; ++i)
        out[i] = x[i] + in[i];
}

/* "expand 32-byte k", the key (4..11), a counter (12, 13), a nonce */
static uint32_t state[16] = {
    0x61707865, 0x3320646e, 0x79622d32, 0x6b206574,
};

static uint32_t pool[8];
static unsigned pool_i;

void random_add(uint32_t value)
{
    pool[pool_i++ % 8] ^= value;
}

/* the pool and the time into the key */
static void random_stir(void)
{
    random_add((uint32_t) arch_cycles());
    random_add((uint32_t) arch_rtime_ns());

    for (int i = 0; i < 8; ++i) {
        state[4 + i] ^= pool[i];
        pool[i] = 0;
    }

    state[14] ^= (uint32_t) (arch_cycles() >> 8);
}

void random_get(void *_buf, size_t len)
{
    uint8_t *buf = _buf;
    uint32_t block[16];

    random_stir();

    while (len) {
        chacha20_block(state, block);

        if (!++state[12])
            ++state[13];

        size_t n = MIN(len, sizeof(block));
        memcpy(buf, block, n);
        buf += n;
        len -= n;
    }

    /* a new key, so what was given can't be found again from the state */
    chacha20_block(state, block);

    if (!++state[12])
        ++state[13];

    for (int i = 0; i < 8; ++i)
        state[4 + i] = block[i];

    memset(block, 0, sizeof(block));
}

static ssize_t randomdev_read(struct devid *dd __unused, off_t offset __unused, size_t size, void *buf)
{
    random_get(buf, size);
    return size;
}

/* what is written is stirred in */
static ssize_t randomdev_write(struct devid *dd __unused, off_t offset __unused, size_t size, void *_buf)
{
    uint8_t *buf = _buf;

    for (size_t i = 0; i < size; ++i)
        random_add((uint32_t) buf[i] << (8 * (i % 4)));

    return size;
}

struct dev randomdev = {
    .read  = randomdev_read,
    .write = randomdev_write,

    .fops  = {
        .open  = posix_file_open,
        .read  = posix_file_read,
        .write = posix_file_write,
        .close = posix_file_close,

        .can_read  = __vfs_can_always,
        .can_write = __vfs_can_always,
        .eof       = __vfs_eof_never,
    },
};
