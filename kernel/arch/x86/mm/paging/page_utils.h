#ifndef _PAGE_UTILS_H
#define _PAGE_UTILS_H

static void *copy_physical_to_virtual(void *_virt_dest, void *_phys_src, size_t n)
{
    //printk("copy_physical_to_virtual(v=%p, p=%p, n=%d)\n", _virt_dest, _phys_src, n);
    char *virt_dest = (char *) _virt_dest;
    char *phys_src  = (char *) _phys_src;

    void *ret = virt_dest;

    /* Copy up to page boundary */
    size_t offset = (uintptr_t) phys_src % PAGE_SIZE;
    size_t size = MIN(n, PAGE_SIZE - offset);
    
    if (size) {
        uintptr_t prev_mount = frame_mount((uintptr_t) phys_src);
        volatile char *p = MOUNT_ADDR;
        memcpy(virt_dest, (char *) (p + offset), size);
        
        phys_src  += size;
        virt_dest += size;

        /* Copy complete pages */
        n -= size;
        size = n / PAGE_SIZE;
        while (size--) {
            frame_mount((uintptr_t) phys_src);
            memcpy(virt_dest, (char *) p, PAGE_SIZE);
            phys_src += PAGE_SIZE;
            virt_dest += PAGE_SIZE;
        }

        /* Copy what is remainig */
        size = n % PAGE_SIZE;
        if (size) {
            frame_mount((uintptr_t) phys_src);
            memcpy(virt_dest, (char *) p, size);
        }

        frame_mount(prev_mount);
    }

    return ret;
}

static void *copy_virtual_to_physical(void *_phys_dest, void *_virt_src, size_t n)
{
    char *phys_dest = (char *) _phys_dest;
    char *virt_src  = (char *) _virt_src;
    void *ret = phys_dest;

    size_t s = n / PAGE_SIZE;
    
    uintptr_t prev_mount = frame_mount(0);

    while (s--) {
        frame_mount((uintptr_t) phys_dest);
        void *p = MOUNT_ADDR;
        memcpy(p, virt_src, PAGE_SIZE);
        phys_dest += PAGE_SIZE;
        virt_src  += PAGE_SIZE;
    }

    s = n % PAGE_SIZE;

    if (s) {
        frame_mount((uintptr_t) phys_dest);
        void *p = MOUNT_ADDR;
        memcpy(p, virt_src, s);
    }

    frame_mount((uintptr_t) prev_mount);
    return ret;
}

#endif
