#ifndef _CORE_ASSERT_H
#define _CORE_ASSERT_H

/* Panics if a condition isn't true, if a size isn't what's expected, or
 * if a pointer isn't aligned as it should be. Each is one statement (a
 * `do { } while (0)`), so that an `else` after one can't bind to its
 * `if`. */

#define assert(x) \
    do { \
        if (!(x)) \
            panic("Assertion " #x " failed"); \
    } while (0)

#define assert_sizeof(o, sz) \
    do { \
        if (sizeof(o) != sz) \
            panic("Assertion sizeof(" #o ") == " #sz " failed"); \
    } while (0)

#define assert_alignof(o, al) \
    do { \
        if (((uintptr_t) o) & (al - 1)) \
            panic("Assertion alignof(" #o ") == " #al " failed"); \
    } while (0)

#endif /* ! _CORE_ASSERT_H */
