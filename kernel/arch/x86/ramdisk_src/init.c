#define SYS_OPEN	3
#define SYS_READ	4
#define SYS_WRITE	5

int syscall(int s, long arg1, long arg2, long arg3)
{
	int ret;
	asm("int $0x80;":"=a"(ret):"a"(s), "b"(arg1), "c"(arg2), "d"(arg3));
	return ret;
}

int open(const char *fn, int flags)
{
	return syscall(SYS_OPEN, fn, flags, 0);
}

int read(int fd, void *buf, int size)
{
	return syscall(SYS_READ, fd, buf, size);
}

int write(int fd, void *buf, int size)
{
	return syscall(SYS_WRITE, fd, buf, size);
}

char kbd_us[] = 
{
	'\0', 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
	'\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
	'\0', 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'',
	'\0', '\0', '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/',
	'\0', '\0', '\0', ' ',
};

void _start()
{
	int console = open("/dev/console", 0);
	int kbd = open("/dev/kbd", 0);

	while(1)
	{
		int scancode;
		if(read(kbd, &scancode, sizeof(scancode)) > 0)
			if(scancode < 0x58)
				write(console, &kbd_us[scancode], 1);
	}
	for(;;);
}