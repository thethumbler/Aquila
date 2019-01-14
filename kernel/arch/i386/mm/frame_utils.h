#ifndef _FRAME_UTILS_H
#define _FRAME_UTILS_H

static inline uintptr_t frame_get()
{
    uintptr_t frame = buddy_alloc(BUDDY_ZONE_NORMAL, PAGE_SIZE);

    if (!frame) {
        panic("Could not allocate frame");
    }

    uintptr_t old = frame_mount(frame);
    volatile void *p = MOUNT_ADDR;
    memset((void *) p, 0, PAGE_SIZE);
    frame_mount(old);

    return frame;
}

static inline uintptr_t frame_get_no_clr()
{
    uintptr_t frame = buddy_alloc(BUDDY_ZONE_NORMAL, PAGE_SIZE);

    if (!frame) {
        panic("Could not allocate frame");
    }

    return frame;
}

static void frame_release(uintptr_t i)
{
    buddy_free(BUDDY_ZONE_NORMAL, i, PAGE_SIZE);
}

#endif
