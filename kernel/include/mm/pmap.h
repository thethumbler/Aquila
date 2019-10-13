#ifndef _MM_PMAP_H
#define _MM_PMAP_H

void pmap_init(void);
struct pmap *pmap_switch(struct pmap *pmap);
struct pmap *pmap_create(void);
void pmap_incref(struct pmap *pmap);
void pmap_decref(struct pmap *pmap);
int  pmap_add(struct pmap *pmap, vaddr_t va, paddr_t pa, uint32_t flags);
void pmap_remove(struct pmap *pmap, vaddr_t sva, vaddr_t eva);
void pmap_protect(struct pmap *pmap, vaddr_t sva, vaddr_t eva, uint32_t prot);
void pmap_page_copy(paddr_t src, paddr_t dst);
void pmap_remove_all(struct pmap *pmap);
int pmap_page_read(paddr_t paddr, off_t off, size_t size, void *buf);
int pmap_page_write(paddr_t paddr, off_t off, size_t size, void *buf);

#endif /* _MM_PMAP_H */
