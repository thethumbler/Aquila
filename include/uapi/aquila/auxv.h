#ifndef _AQUILA_AUXV_H
#define _AQUILA_AUXV_H

/*
 * The auxiliary vector: what the kernel tells a new program about itself
 * and the system, on its initial stack after the environment (see
 * kernel/sys/execve.c). It's a list of (type, value) pairs, each one word,
 * ended by AT_NULL. The types are the System V ABI's, which Linux and the
 * BSDs use too; the C library's getauxval() looks them up.
 */

#define AT_NULL     0   /* the end of the vector */
#define AT_PAGESZ   6   /* the page size, in bytes */
#define AT_ENTRY    9   /* the program's entry point */
#define AT_UID      11  /* the real user ID */
#define AT_EUID     12  /* the effective user ID */
#define AT_GID      13  /* the real group ID */
#define AT_EGID     14  /* the effective group ID */

#endif /* ! _AQUILA_AUXV_H */
