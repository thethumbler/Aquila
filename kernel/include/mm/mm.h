#ifndef _MM_H
#define _MM_H

#include <core/system.h>

#if ARCH==X86
#include <arch/x86/include/mm.h>
#endif

/* Physical Memory Manager interface */

#define KR	_BV(0)	/* Kernel Read */
#define KW	_BV(1)	/* Kernel Write */
#define KX	_BV(2)	/* Kernel eXecute */
#define KRW	(KR|KW)	/* Kernel Read/Write */
#define KRX	(KR|KX)	/* Kernel Read/eXecute */
#define KWX	(KW|KX)	/* Kernel Write/eXecute */
#define KRWX	(KR|KW|KX)	/* Kernel Read/Write/eXecute */


#define UR	_BV(3)	/* User Read */
#define UW	_BV(4)	/* User Write */
#define UX	_BV(5)	/* User eXecute */
#define URW	(UR|UW)	/* User Read/Write */
#define URX	(UR|UX)	/* User Read/eXecute */
#define UWX	(UW|UX)	/* User Write/eXecute */
#define URWX	(UR|UW|UX)	/* User Read/Write/eXecute */

typedef struct
{
	int		(*map)(uintptr_t addr, size_t size, int flags);
	int		(*map_to)(uintptr_t phys, uintptr_t virt, size_t size, int flags);
	void	(*unmap)(uintptr_t addr, size_t size);
	void	(*unmap_full)(uintptr_t addr, size_t size);
#if 0
	void*	(*memcpypp)(void *phys_dest, void *phys_src, size_t n);	/* Phys to Phys memcpy */
#endif
	void*	(*memcpypv)(void *virt_dest, void *phys_src, size_t n);	/* Phys to Virt memcpy */
	void*	(*memcpyvp)(void *phys_dest, void *virt_src, size_t n);	/* Virt to Phys memcpy */
	void*	(*memcpypp)(uintptr_t phys_dest, uintptr_t phys_src, size_t n);	/* Phys to Phys memcpy */
    void    (*switch_mapping)(uintptr_t structue);
    void    (*copy_fork_mapping)(uintptr_t base, uintptr_t fork);
    void    (*handle_page_fault)(uintptr_t addr);
} pmman_t;

struct paging
{
	size_t	size;
	int		(*map)  (uintptr_t addr, size_t nr, int flags);
	void	(*unmap)(uintptr_t addr, size_t nr);
	void*	(*mount)(uintptr_t addr);
} __packed;

extern struct paging paging[NR_PAGE_SIZE];
extern pmman_t pmman;

extern uintptr_t buddy_alloc(size_t);
extern void buddy_free(uintptr_t, size_t);
extern void buddy_dump();
extern void buddy_set_unusable(uintptr_t, size_t);

extern void pmm_lazy_alloc(uintptr_t addr);

extern int debug_kmalloc;

#ifdef DEBUG_KMALLOC
#define kmalloc(s) ((debug_kmalloc? printk("%s [%d]: kmalloc(%d)\n", __func__, __LINE__, (s)): 0), (kmalloc)((s)))
#define kfree(s) ((debug_kmalloc? printk("%s [%d]: kfree(%p)\n", __func__,  __LINE__, (s)): 0), (kfree)((s)))
#endif

void *(kmalloc)(size_t);
void (kfree)(void*);

#endif /* !_MM_H */
