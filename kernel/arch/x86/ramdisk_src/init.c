char *s = "ABCD";

void _start()
{
	int ret;
	asm("int $0x80;":"=a"(ret):"a"(2));
	asm("int $0x80;"::"a"(3), "b"(ret));
	for(;;);
}