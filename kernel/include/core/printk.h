#ifndef _PRINTK_H
#define _PRINTK_H

#define PRINTK_DEBUG

#ifdef PRINTK_DEBUG
int printk(char *fmt, ...);
#else
#define printk(...) {}
#endif

#endif /* !_PRINTK_H */
