#define SYS_EXIT    1
#define SYS_WRITE	18

int write(int fd, void *buf, unsigned n)
{
    int r = SYS_WRITE;
	asm volatile(
            "mov %0, %%eax;"
            "mov %1, %%ebx;"
            "mov %2, %%ecx;"
            "mov %3, %%edx;"
            "int $0x80;"
            :"g"(r), "g"(fd), "g"(buf), "g"(n));
}

void _exit(int status)
{
    int r = SYS_EXIT;
	asm volatile(
            "mov %0, %%eax;"
            "mov %1, %%ebx;"
            "int $0x80;"
            :"g"(r), "g"(status));
}

void _start()
{
    write(1, "Hello, World!\n", 14);
    _exit(0);
}
