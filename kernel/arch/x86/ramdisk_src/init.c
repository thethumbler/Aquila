void _start()
{
	int fd;
	asm("int $0x80;":"=a"(fd):"a"(3), "b"("/dev/tty"));
	for(;;);
}