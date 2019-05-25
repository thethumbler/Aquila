#include <core/system.h>
#include <core/string.h>
#include <mm/mm.h>
#include <fs/ubc.h>

MALLOC_DEFINE(M_UBC, "ubc", "ubc structure");
MALLOC_DEFINE(M_UBC_BUFFER, "ubc-buffer", "ubc buffer");

static void ubc_line_flush(struct ubc *ubc, struct ubc_line *line)
{
    printk("ubc_line_flush(ubc=%p, line=%p)\n", ubc, line);

    if (line->data)
        ubc->flush(ubc->p, line->tag, line->data);
}

static void ubc_line_fill(struct ubc *ubc, struct ubc_line *line)
{
    printk("ubc_line_fill(ubc=%p, line=%p)\n", ubc, line);

    if (!line->data) {
        /* TODO error checking */
        line->data = kmalloc(ubc->bs, &M_UBC_BUFFER, 0);
    }

    ubc->fill(ubc->p, line->tag, line->data);
}

struct ubc *ubc_new(size_t bs, void *p, void *fill, void *flush)
{
    struct ubc *ubc = kmalloc(sizeof(ubc), &M_UBC, 0);

    if (!ubc)
        return NULL;

    memset(ubc, 0, sizeof(struct ubc));

    ubc->bs    = bs;
    ubc->p     = p;
    ubc->fill  = fill;
    ubc->flush = flush;

    return ubc;
} 

void ubc_free(struct ubc *ubc)
{
    /* Flush all lines */
    for (int i = 0; i < UBC_LINES; ++i) {
        for (int j = 0; j < UBC_ASSOC; ++j) {
            struct ubc_line *line = &ubc->lines[i][j];

            if (line->data)
                ubc_line_flush(ubc, line);

            kfree(line->data);
        }
    }

    kfree(ubc);
} 

ssize_t ubc_read(struct ubc *ubc, uintptr_t block, void *buf)
{
    printk("ubc_read(ubc=%p, block=%p, buf=%p)\n", ubc, block, buf);

    size_t line_idx = block % UBC_LINES;

    for (int i = 0; i < UBC_ASSOC; ++i) {
        struct ubc_line *line = &ubc->lines[line_idx][i];

        if (line->tag == block) {
            printk("cache hit!\n");
            /* Update access time */
            line->access = ++ubc->time;
            memcpy(buf, line->data, ubc->bs);
            return ubc->bs;
        }
    }

    printk("cache miss!\n");
    /* Not found -- flush and fill */
    struct ubc_line *oldest_line = &ubc->lines[line_idx][0];

    for (int i = 1; i < UBC_ASSOC; ++i) {
        struct ubc_line *line = &ubc->lines[line_idx][i];
        if (line->access < oldest_line->access)
            oldest_line = line;
    }

    ubc_line_flush(ubc, oldest_line);
    oldest_line->tag = block;
    ubc_line_fill(ubc, oldest_line);
    oldest_line->access = ++ubc->time;
    memcpy(buf, oldest_line->data, ubc->bs);
    return 0;
    //return ubc->bs;
}

ssize_t ubc_write(struct ubc *ubc, uintptr_t block, void *buf)
{
    printk("ubc_write(ubc=%p, block=%p, buf=%p)\n", ubc, block, buf);

    size_t line_idx = block % UBC_LINES;

    for (int i = 0; i < UBC_ASSOC; ++i) {
        struct ubc_line *line = &ubc->lines[line_idx][i];

        if (line->tag == block) {
            /* Update access time */
            printk("cache hit!\n");
            line->access = ++ubc->time;
            memcpy(line->data, buf, ubc->bs);
            return ubc->bs;
        }
    }

    printk("cache miss!\n");
    /* Not found -- flush and fill */
    struct ubc_line *oldest_line = &ubc->lines[line_idx][0];

    for (int i = 1; i < UBC_ASSOC; ++i) {
        struct ubc_line *line = &ubc->lines[line_idx][i];
        if (line->access < oldest_line->access)
            oldest_line = line;
    }

    ubc_line_flush(ubc, oldest_line);

    if (!oldest_line->data)
        oldest_line->data = kmalloc(ubc->bs, &M_UBC_BUFFER, 0);

    oldest_line->tag = block;
    memcpy(oldest_line->data, buf, ubc->bs);
    oldest_line->access = ++ubc->time;
    return 0;
    //return ubc->bs;
}
