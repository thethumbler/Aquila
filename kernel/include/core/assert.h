#ifndef _CORE_ASSERT_H
#define _CORE_ASSERT_H

#define assert(x) \
    if (!(x)) \
        panic("Assertion " #x " failed");

#define assert_sizeof(o, sz) \
    if (sizeof(o) != sz) \
        panic("Assertion sizeof(" #o ") == " #sz " failed");

#define assert_alignof(o, al) \
    if (((uintptr_t) o) & (al - 1)) \
        panic("Assertion alignof(" #o ") == " #al " failed");

#endif /* ! _CORE_ASSERT_H */
