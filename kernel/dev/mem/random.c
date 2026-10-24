/** \file
 * Random numbers: `/dev/random` and `/dev/urandom` (character devices 1,
 * 8 and 1, 9), which are the same, and `random_get()` for the kernel.
 *
 * The generator is ChaCha20. Its key is made from a pool that the cycle
 * counter is stirred into on every timer tick, and at each request. After
 * each request, the key is replaced with keystream that nobody saw, so
 * that what was given can't be found again. There is no hardware source:
 * the randomness comes from the timing of interrupts and requests. That
 * is unpredictable enough for hash seeds and `arc4random()`, but it isn't
 * a strong key generator.
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

/** Computes one ChaCha20 block (20 rounds) of the state `in`, into `out`. */
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

/* ChaCha20's state: the constant "expand 32-byte k", the key (words 4 to
 * 11), a counter (12, 13) and a nonce (14, 15) */
static uint32_t state[16] = {
    0x61707865, 0x3320646e, 0x79622d32, 0x6b206574,
};

static uint32_t pool[8];
static unsigned pool_i;

/**
 * Stirs a value into the pool, which goes into the next key. The timer
 * tick adds the cycle counter on each tick.
 *
 * @param value  the value, which should be hard to predict
 */
void random_add(uint32_t value)
{
    pool[pool_i++ % 8] ^= value;
}

/** Mixes the pool, and the time, into the key, and empties the pool. */
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

/**
 * Fills a buffer with random bytes.
 *
 * @param _buf  the buffer
 * @param len   its size, in bytes
 */
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

    /* a new key, so that what was given can't be found again from the
     * state */
    chacha20_block(state, block);

    if (!++state[12])
        ++state[13];

    for (int i = 0; i < 8; ++i)
        state[4 + i] = block[i];

    memset(block, 0, sizeof(block));
}

/** Reads random bytes, a buffer at a time; the buffer is wiped after. */
static ssize_t randomdev_read(struct devid *dd __unused, off_t offset __unused, struct iobuf *io)
{
    uint8_t buf[256];
    size_t size = io->resid, done = 0;

    while (done < size) {
        size_t len = MIN(size - done, sizeof(buf));
        random_get(buf, len);

        ssize_t r = iobuf_out(io, buf, len);

        if (r < 0) {
            memset(buf, 0, sizeof(buf));
            return done ? (ssize_t) done : r;
        }

        done += r;

        if ((size_t) r < len)
            break;
    }

    memset(buf, 0, sizeof(buf));
    return done;
}

/** Stirs what is written into the pool. */
static ssize_t randomdev_write(struct devid *dd __unused, off_t offset __unused, struct iobuf *io)
{
    uint8_t buf[256];
    size_t size = io->resid, done = 0;

    while (done < size) {
        ssize_t len = iobuf_in(io, buf, MIN(size - done, sizeof(buf)));

        if (len <= 0)
            return done ? (ssize_t) done : len;

        for (ssize_t i = 0; i < len; ++i)
            random_add((uint32_t) buf[i] << (8 * ((done + i) % 4)));

        done += len;
    }

    return done;
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
