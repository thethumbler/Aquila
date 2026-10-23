#ifndef _AARCH64_SYSTEM_H
#define _AARCH64_SYSTEM_H

#include <config.h>

/* 4 KiB pages (the translation granule, TCR_EL1.TG0/TG1) */
#define PAGE_SIZE   (0x1000)
#define PAGE_MASK   (PAGE_SIZE - 1)
#define PAGE_SHIFT  (12)

#include_next <core/system.h>

#endif /* ! _AARCH64_SYSTEM_H */
