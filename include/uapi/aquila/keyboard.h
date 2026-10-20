#ifndef _AQUILA_KEYBOARD_H
#define _AQUILA_KEYBOARD_H

/* what the keys of the console's keyboard map to (KDGKBENT), the values
 * of Linux's <linux/keyboard.h>: a type and a value */

#define NR_KEYS     256

#define K(t, v)     (((t) << 8) | (v))
#define KTYP(x)     ((x) >> 8)
#define KVAL(x)     ((x) & 0xff)

#define KT_LATIN    0   /* a character */
#define KT_FN       1   /* function and editing keys */
#define KT_SPEC     2
#define KT_PAD      3   /* keypad */
#define KT_DEAD     4
#define KT_CONS     5
#define KT_CUR      6   /* cursor keys */
#define KT_SHIFT    7   /* modifiers */
#define KT_META     8
#define KT_ASCII    9
#define KT_LOCK     10
#define KT_LETTER   11  /* a letter: caps lock applies */

/* the modifiers, bits of the table (struct kbentry.kb_table) */
#define KG_SHIFT    0
#define KG_ALTGR    1
#define KG_CTRL     2
#define KG_ALT      3
#define KG_SHIFTL   4
#define KG_SHIFTR   5
#define KG_CTRLL    6
#define KG_CTRLR    7

#define K_F1        K(KT_FN, 0)     /* to K_F12, K(KT_FN, 11) */
#define K_FIND      K(KT_FN, 20)    /* Home */
#define K_INSERT    K(KT_FN, 21)
#define K_REMOVE    K(KT_FN, 22)    /* Delete */
#define K_SELECT    K(KT_FN, 23)    /* End */
#define K_PGUP      K(KT_FN, 24)
#define K_PGDN      K(KT_FN, 25)
#define K_MACRO     K(KT_FN, 26)
#define K_HELP      K(KT_FN, 27)
#define K_DO        K(KT_FN, 28)
#define K_PAUSE     K(KT_FN, 29)

#define K_HOLE      K(KT_SPEC, 0)   /* no key */
#define K_ENTER     K(KT_SPEC, 1)
#define K_BREAK     K(KT_SPEC, 5)
#define K_CAPS      K(KT_SPEC, 7)
#define K_NUM       K(KT_SPEC, 8)
#define K_HOLD      K(KT_SPEC, 9)   /* Scroll Lock */
#define K_COMPOSE   K(KT_SPEC, 14)
#define K_NOSUCHMAP K(KT_SPEC, 127)

#define K_P0        K(KT_PAD, 0)    /* to K_P9, K(KT_PAD, 9) */
#define K_PPLUS     K(KT_PAD, 10)
#define K_PMINUS    K(KT_PAD, 11)
#define K_PSTAR     K(KT_PAD, 12)
#define K_PSLASH    K(KT_PAD, 13)
#define K_PENTER    K(KT_PAD, 14)
#define K_PCOMMA    K(KT_PAD, 15)
#define K_PDOT      K(KT_PAD, 16)
#define K_PPLUSMINUS K(KT_PAD, 17)

#define K_DGRAVE    K(KT_DEAD, 0)
#define K_DACUTE    K(KT_DEAD, 1)
#define K_DCIRCM    K(KT_DEAD, 2)
#define K_DTILDE    K(KT_DEAD, 3)
#define K_DDIERE    K(KT_DEAD, 4)

#define K_DOWN      K(KT_CUR, 0)
#define K_LEFT      K(KT_CUR, 1)
#define K_RIGHT     K(KT_CUR, 2)
#define K_UP        K(KT_CUR, 3)

#define K_SHIFT     K(KT_SHIFT, KG_SHIFT)
#define K_ALTGR     K(KT_SHIFT, KG_ALTGR)
#define K_CTRL      K(KT_SHIFT, KG_CTRL)
#define K_ALT       K(KT_SHIFT, KG_ALT)
#define K_SHIFTL    K(KT_SHIFT, KG_SHIFTL)
#define K_SHIFTR    K(KT_SHIFT, KG_SHIFTR)
#define K_CTRLL     K(KT_SHIFT, KG_CTRLL)
#define K_CTRLR     K(KT_SHIFT, KG_CTRLR)

#define K_SHIFTLOCK K(KT_LOCK, KG_SHIFT)

#endif /* ! _AQUILA_KEYBOARD_H */
