#ifndef __PTHREAD_SYSCALLS
#define __PTHREAD_SYSCALLS

#define SYS_THREAD_CREATE   29
#define SYS_THREAD_EXIT     30
#define SYS_THREAD_JOIN     31

#define SYSCALL2(ret, v, arg1, arg2) \
	asm volatile("int $0x80;":"=a"(ret):"a"(v), "b"(arg1), "c"(arg2));
#define SYSCALL1(ret, v, arg1) \
	asm volatile("int $0x80;":"=a"(ret):"a"(v), "b"(arg1));
#define SYSCALL0(ret, v) \
	asm volatile("int $0x80;":"=a"(ret):"a"(v));

#endif
