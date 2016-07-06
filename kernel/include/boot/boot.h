#ifndef _BOOT_H
#define _BOOT_H

#include <core/system.h>
#include <boot/multiboot.h>

typedef struct {
	void *addr;
	size_t size;
	char *cmdline;
} module_t;

typedef struct {
	uintptr_t start;
	uintptr_t end;
} mmap_t;


struct boot {
	char *cmdline;
	uintptr_t total_mem;
	
	int modules_count;
	int mmap_count;
	module_t *modules;
	mmap_t *mmap;
};

static inline int get_multiboot_mmap_count(multiboot_info_t *info)
{
	int count = 0;
	uint32_t _mmap = info->mmap_addr;
	multiboot_mmap_t *mmap = (multiboot_mmap_t*)_mmap;
	uint32_t mmap_end = _mmap + info->mmap_length;

	while (_mmap < mmap_end) {
		if (mmap->type == MULTIBOOT_MEMORY_AVAILABLE)
			++count;
		_mmap += mmap->size + sizeof(uint32_t);
		mmap   = (multiboot_mmap_t*) _mmap;
	}
	return count;
}

static inline void 
build_multiboot_mmap(multiboot_info_t *info, mmap_t *boot_mmap)
{
	uint32_t _mmap = info->mmap_addr;
	multiboot_mmap_t *mmap = (multiboot_mmap_t *) _mmap;
	uint32_t mmap_end = _mmap + info->mmap_length;

	while (_mmap < mmap_end) {
		if (mmap->type == MULTIBOOT_MEMORY_AVAILABLE) {
			*boot_mmap = (mmap_t) {
				.start = mmap->addr,
				.end = mmap->addr + mmap->len
			};

			++boot_mmap;
		}
		_mmap += mmap->size + sizeof(uint32_t);
		mmap   = (multiboot_mmap_t*) _mmap;
	}
}

static inline void 
build_multiboot_modules(multiboot_info_t *info, module_t *modules)
{
	multiboot_module_t *mods = (multiboot_module_t *) info->mods_addr;

	for (unsigned i = 0; i < info->mods_count; ++i) {
		modules[i] = (module_t) {
			.addr = (void *) VMA(mods[i].mod_start),
			.size = mods[i].mod_end - mods[i].mod_start,
			.cmdline = (char *) VMA(mods[i].cmdline)
		};
	}
}

static inline struct boot *process_multiboot_info(multiboot_info_t *info)
{
	static struct boot boot;

	boot.cmdline = (char *) VMA(info->cmdline);
	boot.total_mem = info->mem_lower + info->mem_upper;
	
	boot.mmap_count = get_multiboot_mmap_count(info);
	static mmap_t mmap[32] = {0};
	boot.mmap = mmap;
	build_multiboot_mmap(info, boot.mmap);

	boot.modules_count = info->mods_count;
	static module_t modules[32] = {0};
	boot.modules = modules;
	build_multiboot_modules(info, boot.modules);

	return &boot;
}

#endif