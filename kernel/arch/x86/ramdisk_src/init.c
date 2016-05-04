char *s = "ABCD";

void _start()
{
	int ret;
	asm("int $0x80;"::"a"(2));
	asm("mov %%eax, %0"::"g"(ret));
	//asm("int $0x80;"::"a"(3), "b"(ret));
	for(;;);
}