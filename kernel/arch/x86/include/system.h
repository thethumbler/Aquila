#ifndef _X86_SYSTEM_H
#define _X86_SYSTEM_H

#define PAGE_SIZE	(0x1000)
#define PAGE_MASK	(PAGE_SIZE - 1)
#define PAGE_SHIFT	(12)
#define TABLE_SIZE	(0x400 * PAGE_SIZE)
#define TABLE_MASK	(TABLE_SIZE - 1)

#endif /* !_X86_SYSTEM_H */
