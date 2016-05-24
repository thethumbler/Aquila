#include <core/system.h>
#include <console/early_console.h>
#include <core/printk.h>
#include <cpu/cpu.h>
#include <core/string.h>
#include <mm/mm.h>

#include <boot/multiboot.h>
#include <boot/boot.h>

char *kernel_cmdline;
int kernel_modules_count;
uintptr_t kernel_total_mem;
int kernel_mmap_count;
module_t *kernel_modules;
mmap_t *kernel_mmap;

void *_kernel_heap;
static void *heap_alloc(size_t size, size_t align)
{
	void *ret = (void*)((uintptr_t)(_kernel_heap + align - 1) & (~(align - 1)));
	_kernel_heap = (void*)((uintptr_t)ret + size);

	memset(ret, 0, size);	/* We always clear the allocated area */

	return ret;
}

static int get_multiboot_mmap_count()
{
	int count = 0;
	uint32_t _mmap = multiboot_info->mmap_addr;
	multiboot_mmap_t *mmap = (multiboot_mmap_t*)_mmap;
	uint32_t mmap_end = _mmap + multiboot_info->mmap_length;

	while(_mmap < mmap_end)
	{
		if(mmap->type == MULTIBOOT_MEMORY_AVAILABLE)
			++count;
		_mmap += mmap->size + sizeof(uint32_t);
		mmap   = (multiboot_mmap_t*) _mmap;
	}
	return count;
}

static uintptr_t get_multiboot_total_mem()
{
	uintptr_t total_mem = 0;
	uint32_t _mmap = multiboot_info->mmap_addr;
	multiboot_mmap_t *mmap = (multiboot_mmap_t*)_mmap;
	uint32_t mmap_end = _mmap + multiboot_info->mmap_length;

	while(_mmap < mmap_end)
	{
		total_mem += mmap->len;
		_mmap += mmap->size + sizeof(uint32_t);
		mmap   = (multiboot_mmap_t*) _mmap;
	}

	return total_mem;
}

static void build_multiboot_mmap(mmap_t *boot_mmap)	/* boot_mmap must be allocated beforehand */
{
	uint32_t _mmap = multiboot_info->mmap_addr;
	multiboot_mmap_t *mmap = (multiboot_mmap_t*)_mmap;
	uint32_t mmap_end = _mmap + multiboot_info->mmap_length;

	while(_mmap < mmap_end)
	{
		if(mmap->type == MULTIBOOT_MEMORY_AVAILABLE)
		{
			*boot_mmap = (mmap_t)
			{
				.start = mmap->addr,
				.end = mmap->addr + mmap->len
			};

			++boot_mmap;
		}
		_mmap += mmap->size + sizeof(uint32_t);
		mmap   = (multiboot_mmap_t*) _mmap;
	}
}

static int get_multiboot_modules_count()
{
	return multiboot_info->mods_count;
}

static void build_multiboot_modules(module_t *modules)	/* modules must be allocated beforehand */
{
	multiboot_module_t *mods = (multiboot_module_t *) multiboot_info->mods_addr;

	for(unsigned i = 0; i < multiboot_info->mods_count; ++i)
	{
		modules[i] = (module_t)
		{
			.addr = (void *) VMA(mods[i].mod_start),
			.size = mods[i].mod_end - mods[i].mod_start,
			.cmdline = (char *) VMA(mods[i].cmdline)
		};
	}
}

module_t dummy_buf[20];

void process_multiboot_info()
{
	kernel_cmdline = (char *) multiboot_info->cmdline;
	kernel_total_mem = get_multiboot_total_mem();
	kernel_mmap_count = get_multiboot_mmap_count();
	kernel_mmap = heap_alloc(kernel_mmap_count * sizeof(mmap_t), 1);
	build_multiboot_mmap(kernel_mmap);
	kernel_modules_count = get_multiboot_modules_count();
	kernel_modules = dummy_buf; //heap_alloc(kernel_modules_count * sizeof(module_t), 1);
	build_multiboot_modules(kernel_modules);
}

volatile uint32_t *BSP_PD = NULL;
volatile uint32_t *BSP_LPT = NULL;

void cpu_init()
{
	extern uint32_t *_BSP_PD;
	extern uint32_t *_BSP_LPT;
	BSP_PD = VMA(_BSP_PD);
	BSP_LPT = VMA(_BSP_LPT);

	extern void *kernel_heap;
	_kernel_heap = VMA(kernel_heap);

	process_multiboot_info();

	x86_gdt_setup();
	early_console_init();
	printk("Hello, World!\n");

	x86_idt_setup();
	x86_isr_setup();
	x86_pic_setup();
	x86_mm_setup();
	x86_vmm_setup();

	for(int i = 0; BSP_PD[i] != 0; ++i) BSP_PD[i] = 0;	/* Unmap lower half */
	TLB_flush();

	extern void x86_set_tss_esp(uint32_t esp);
	x86_set_tss_esp(VMA(0x100000));

	x86_pic_setup();
	x86_pit_setup(20);

	extern void kmain();
	kmain();

	for(;;);

	struct cpu_features *fp = x86_get_features();
	if(fp->apic)
	{
		printk("CPU has Local APIC\n");
		extern void trampoline();
		extern void trampoline_end();

		printk("Copying trampoline code [%d]\n", trampoline_end - trampoline);
		memcpy(0, trampoline, trampoline_end - trampoline);

		for(;;);
	}

	for(;;);
}

#if 0
struct apic_icr
{
	uint32_t vector	: 8;
	uint32_t dest	: 3;
	uint32_t mode	: 1;
	uint32_t status	: 1;
	uint32_t _res	: 1;
	uint32_t level	: 1;
	uint32_t triger	: 1;
	uint32_t __res	: 2;
	uint32_t shorthand	: 2;
	uint32_t ___res	: 12;
}__attribute__((packed));

void x86_ap_init()
{
	x86_gdt_setup();
	x86_idt_setup();

	void x86_ap_mm_setup();
	x86_ap_mm_setup();
	for(;;);
}

void x86_cpu_init()
{
	for(;;);
	early_console_init();
	x86_gdt_setup();
	x86_idt_setup();
	x86_isr_setup();

	x86_pic_setup();
	x86_pit_setup(20);

	void x86_mm_setup();
	x86_mm_setup();

	for(;;);

	extern void kmain();
	//kmain();

	void sleep(uint32_t ms);

	struct cpu_features *fp = x86_get_features();
	if(fp->apic)
	{
		printk("CPU has Local APIC\n");

		extern void trampoline();
		extern void trampoline_end();

		printk("Copying trampoline code [%d]\n", trampoline_end - trampoline);
		memcpy(0, trampoline, trampoline_end - trampoline);

		for(;;);

		extern uint32_t ap_done;
		//x86_get_cpu_count();

		printk("Init APs\n", trampoline);
		//for(;;);

		asm("sti");

		uint32_t volatile *apic = (uint32_t*) (0xFFFFF000 & (uint32_t)x86_msr_read(X86_APIC_BASE));

		apic[0xF0/4] = apic[0xF0/4] | 0x100;

		apic[0x310/4] = 1 << 24;
		apic[0x300/4] = 0x4500;

		sleep(10);

		apic[0x300/4] = 0x4600;// | ((uint32_t)(trampoline)/0x1000);

		sleep(100);

		if(ap_done > 0)
		{
			printk("AP is up\n");
			goto done;
		}

		apic[0x300/4] = 0x4600;	// | ((uint32_t)(trampoline)/0x1000);

		sleep(1000);

		if(ap_done > 0)
		{
			printk("AP is up 2nd\n");
			for(;;);
		}

		done:

		asm("cli");
		x86_pic_disable();


		for(;;);

		//x86_get_cpu_count();

		//x86_apic_setup();
		//asm("sti;");

		//*(uint32_t volatile *) 0xFEE00310 = 1 << 24;
		//*(uint32_t volatile *) 0xFEE00300 = 2;// << 10;
	}

	for(;;);
	//printk("%x", x86_get_acpi_madt());

	printk("%d", x86_get_cpu_count());

	//x86_mm_setup();
	//
	//x86_idt_setup();
	for(;;);
}
#endif
