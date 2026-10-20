#ifndef _SHADOW_H
#define _SHADOW_H

#include <stdio.h>

#define SHADOW  "/etc/shadow"

/* an /etc/shadow entry, -1 for empty numeric fields */
struct spwd {
    char *sp_namp;              /* login name */
    char *sp_pwdp;              /* password hash, see crypt(3) */
    long  sp_lstchg;            /* date of last change (days since the epoch) */
    long  sp_min;               /* minimum days between changes */
    long  sp_max;               /* maximum days between changes */
    long  sp_warn;              /* days of warning before expiry */
    long  sp_inact;             /* days after expiry until the account is disabled */
    long  sp_expire;            /* date the account expires (days since the epoch) */
    unsigned long sp_flag;      /* reserved */
};

struct spwd *getspnam(const char *name);
struct spwd *getspent(void);
void setspent(void);
void endspent(void);

#endif /* ! _SHADOW_H */
