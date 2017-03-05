#include <stdarg.h>
#include <stdint.h>

#define SYS_WRITE	5

int syscall(int s, long arg1, long arg2, long arg3)
{
	int ret;
	asm("int $0x80;":"=a"(ret):"a"(s), "b"(arg1), "c"(arg2), "d"(arg3));
	return ret;
}

int write(int fd, void *buf, int size)
{
	return syscall(SYS_WRITE, fd, buf, size);
}

int printf(char *fmt, ...);

int main(int argc, char **argv)
{
	printf("Hello World!, from Aquila OS :D\n");
	printf("argc %d\n", argc);
	printf("argv %x\n", argv);
    for (int i = 0; i < argc; ++i)
        printf("argv[%d]: %s\n", i, argv[i]);
	for(;;);
}

asm(" \
.globl _start \n\
_start: \n\
	pop %eax \n\
	mov %esp, %ebx \n\
	push %ebx \n\
	push %eax \n\
	call main \n\
	jmp . \n\
");


static int putc(char c)
{
	return write(1, &c, 1);
}

int strlen(char *s)
{
	int i;
	for(i = 0; s[i] != 0; ++i);
	return i;	
}

static int puts(char *s)
{
	return write(1, s, strlen(s));
}

static int putx(uint32_t val)
{
	char *enc = "0123456789ABCDEF";
	char buf[9];
	buf[8] = '\0';
	uint8_t i = 8;
	while(i)
	{
		buf[--i] = enc[val&0xF];
		val >>= 4;
	}
	return puts(buf);
}

static int putlx(uint64_t val)
{
	char *enc = "0123456789ABCDEF";
	char buf[17];
	buf[16] = '\0';
	uint8_t i = 16;
	while(i)
	{
		buf[--i] = enc[val&0xF];
		val >>= 4;
	}
	return puts(buf);
}

static int putud(uint32_t val)
{
	char buf[11];
	buf[10] = '\0';
	if(!val) { buf[9] = '0'; return puts(&buf[9]); }
	uint8_t i = 10;
	while(val)
	{
		buf[--i] = val%10 + '0';
		val = (val-val%10)/10;
	}
	return puts(buf+i);
}

static int putul(uint64_t val)
{
	char buf[21];
	buf[20] = '\0';
	uint8_t i = 20;
	while(val)
	{
		buf[--i] = val%10 + '0';
		val = (val-val%10)/10;
	}
	return puts(buf+i);
}

static int putb(uint8_t val)
{
	char buf[9];
	buf[8] = '\0';
	uint8_t i = 8;
	while(i)
	{
		buf[--i] = '0' + (val & 1);
		val >>= 1;
	}
	return puts(buf);
}

int vprintf(char *fmt, va_list args)
{
	int ret = 0;
	while(*fmt)
	switch(*fmt)
	{
		case '%':
			++fmt;
			switch(*fmt)
			{
				case 'c':	/* char */
					ret += putc((char)va_arg(args, int));
					break;
				case 's':	/* char * */
					ret += puts((char*)va_arg(args, char*));
					break;
				case 'd': /* decimal */
					ret += putud((uint32_t)va_arg(args, uint32_t));
					break;
				case 'l':	/* long */
					switch(*++fmt)
					{
						case 'x':	/* long hex */
							ret += putlx((uint64_t)va_arg(args, uint64_t));
							break;
						case 'd':
							ret += putul((uint64_t)va_arg(args, uint64_t));
							break;
						default:
							ret += putc(*--fmt);
					}
					break;
				
				case 'b': /* binary */
					ret += putb((uint8_t)(uint32_t)va_arg(args, uint32_t));
					break;
				case 'x': /* Hexadecimal */
					ret += putx((uint32_t)va_arg(args, uint32_t));
					break;
				default:
					ret += putc(*(--fmt));
			}
			++fmt;
			break;
		default:
			ret += putc(*fmt);
			++fmt;
	}

	return ret;
}

int printf(char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	int ret = vprintf(fmt, args);
	va_end(args);
	return ret;
}
