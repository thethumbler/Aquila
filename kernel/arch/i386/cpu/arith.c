/*
 * https://www.ida.liu.se/~TDIU25/pintos/src/lib/arithmetic.c
 */

#include <stdint.h>

/* Uses x86 DIVL instruction to divide 64-bit N by 32-bit D to
     yield a 32-bit quotient.  Returns the quotient.
     Traps with a divide error (#DE) if the quotient does not fit
     in 32 bits. */
static inline uint32_t divl(uint64_t n, uint32_t d)
{
    uint32_t n1 = n >> 32;
    uint32_t n0 = n;
    uint32_t q, r;

    asm ("divl %4"
             : "=d" (r), "=a" (q)
             : "0" (n1), "1" (n0), "rm" (d));

    return q;
}

/* Returns the number of leading zero bits in X,
     which must be nonzero. */
static int nlz(uint32_t x) 
{
    int n = 0;
    if (x <= 0x0000FFFF) {
        n += 16;
        x <<= 16; 
    }

    if (x <= 0x00FFFFFF) {
        n += 8;
        x <<= 8; 
    }

    if (x <= 0x0FFFFFFF) {
        n += 4;
        x <<= 4;
    }

    if (x <= 0x3FFFFFFF) {
        n += 2;
        x <<= 2; 
    }

    if (x <= 0x7FFFFFFF)
        n++;

    return n;
}

/* Divides unsigned 64-bit N by unsigned 64-bit D and returns the
     quotient. */
static uint64_t udiv64(uint64_t n, uint64_t d)
{
    if ((d >> 32) == 0) {
        /* Proof of correctness:
         * Let n, d, b, n1, and n0 be defined as in this function.
         * Let [x] be the "floor" of x.  Let T = b[n1/d].  Assume d
         * nonzero.  Then:
         *   [n/d] = [n/d] - T + T
         *         = [n/d - T] + T                      by (1) below
         *         = [(b*n1 + n0)/d - T] + T            by definition of n
         *         = [(b*n1 + n0)/d - dT/d] + T
         *         = [(b(n1 - d[n1/d]) + n0)/d] + T
         *         = [(b[n1 % d] + n0)/d] + T,          by definition of %
         * which is the expression calculated below.
         *
         * (1) Note that for any real x, integer i: [x] + i = [x + i].
         *
         * To prevent divl() from trapping, [(b[n1 % d] + n0)/d] must
         * be less than b.    Assume that [n1 % d] and n0 take their
         * respective maximum values of d - 1 and b - 1:
         *   [(b(d - 1) + (b - 1))/d] < b
         *   <=> [(bd - 1)/d] < b
         *   <=> [b - 1/d] < b
         * which is a tautology.
         *
         * Therefore, this code is correct and will not trap.
         */

        uint64_t b = 1ULL << 32;
        uint32_t n1 = n >> 32;
        uint32_t n0 = n; 
        uint32_t d0 = d;

        return divl(b * (n1 % d0) + n0, d0) + b * (n1 / d0); 
    } else {
        /* Based on the algorithm and proof available from
         * http://www.hackersdelight.org/revisions.pdf.
         */
        if (n < d) {
            return 0;
        } else {
            uint32_t d1 = d >> 32;
            int s = nlz(d1);
            uint64_t q = divl(n >> 1, (d << s) >> 32) >> (31 - s);
            return n - (q - 1) * d < d ? q - 1 : q; 
        }
    }
}

/* Divides unsigned 64-bit N by unsigned 64-bit D and returns the
     remainder. */
static uint32_t umod64(uint64_t n, uint64_t d)
{
    return n - d * udiv64(n, d);
}

/* Divides signed 64-bit N by signed 64-bit D and returns the
     quotient. */
static int64_t sdiv64(int64_t n, int64_t d)
{
    uint64_t n_abs = n >= 0 ? (uint64_t) n : -(uint64_t) n;
    uint64_t d_abs = d >= 0 ? (uint64_t) d : -(uint64_t) d;
    uint64_t q_abs = udiv64 (n_abs, d_abs);
    return (n < 0) == (d < 0) ? (int64_t) q_abs : -(int64_t) q_abs;
}

/* Divides signed 64-bit N by signed 64-bit D and returns the
     remainder. */
static int32_t smod64(int64_t n, int64_t d)
{
    return n - d * sdiv64(n, d);
}

/* These are the routines that GCC calls. */

long long __divdi3(long long n, long long d);
long long __moddi3(long long n, long long d);
unsigned long long __udivdi3(unsigned long long n, unsigned long long d);
unsigned long long __umoddi3(unsigned long long n, unsigned long long d);

/* Signed 64-bit division. */
long long __divdi3(long long n, long long d) 
{
    return sdiv64(n, d);
}

/* Signed 64-bit remainder. */
long long __moddi3(long long n, long long d) 
{
    return smod64 (n, d);
}

/* Unsigned 64-bit division. */
unsigned long long __udivdi3(unsigned long long n, unsigned long long d) 
{
    return udiv64 (n, d);
}

/* Unsigned 64-bit remainder. */
unsigned long long __umoddi3(unsigned long long n, unsigned long long d) 
{
    return umod64 (n, d);
}
