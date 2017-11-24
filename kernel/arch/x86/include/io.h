#ifndef _X86_IO_H
#define _X86_IO_H

#include <stdint.h>

#define inb(port) \
({ \
	uint8_t ret; \
	asm volatile ("inb %%dx, %%al":"=a"(ret):"d"(port)); \
	ret; \
})

#define inw(port) \
({ \
	uint16_t ret; \
	asm volatile ("inw %1, %0":"=a"(ret):"dN"(port)); \
	ret; \
})

#define inl(port) \
({ \
	uint32_t ret; \
	asm volatile ("inl %%dx, %%eax":"=a"(ret):"d"(port)); \
	ret; \
})

#define insw(port, count, buf) \
({ \
	asm volatile ("rep insw"::"D"(buf), "c"(count), "d"(port):"memory"); \
})

#define outb(port, value) \
({ \
	asm volatile ("outb %%al, %%dx"::"d"((port)),"a"((value))); \
})

#define outw(port, value) \
({ \
	asm volatile ("outw %%ax, %%dx"::"d"(port),"a"(value)); \
})

#define outl(port, value) \
({ \
	asm volatile ("outl %%eax, %%dx"::"d"(port),"a"(value)); \
})

#define io_wait() \
({ \
    asm volatile ( "jmp 1f\n\t" \
                   "1:jmp 2f\n\t" \
                   "2:" ); \
})

#endif /* !_X86_IO_H */
