/*
 *          PS/2 Mouse driver
 *
 *
 *  This file is part of Aquila OS and is released under
 *  the terms of GNU GPLv3 - See LICENSE.
 *
 *  Copyright (C) Mohamed Anwar
 */


#include <core/system.h>
#include <core/arch.h>
#include <cpu/cpu.h>

#include <chipset/misc.h>

#include <sys/proc.h>
#include <sys/sched.h>

#include <dev/dev.h>
#include <fs/devfs.h>
#include <fs/posix.h>

#include <ds/ringbuf.h>
#include <ds/queue.h>

#include <bits/errno.h>

struct dev ps2mousedev;

struct ioaddr mouse = {
    .addr = 0x60,
    .type = IOADDR_PORT,
};

#define MOUSE_CMD  0x04
#define MOUSE_DATA 0x00

#define SEND_CMD            0xD4
#define ENABLE_AUX_PS2      0xA8
#define GET_COMPAQ_STATUS   0x20
#define SET_COMPAQ_STATUS   0x60
#define USE_DEFAULTS        0xF6
#define ENABLE_MOUSE        0xF4

#define BUF_SIZE    128

static struct ringbuf *mouse_ring = RINGBUF_NEW(BUF_SIZE);   /* Mouse ring Buffer */
static proc_t *proc = NULL; /* Current process using mouse */
static queue_t *mouse_read_queue = QUEUE_NEW(); /* Mouse read queue */

struct mouse_packet {
    uint8_t left    : 1;
    uint8_t right   : 1;
    uint8_t mid     : 1;
    uint8_t _1      : 1;
    uint8_t x_sign  : 1;
    uint8_t y_sign  : 1;
    uint8_t x_over  : 1;
    uint8_t y_over  : 1;
} __packed;

void ps2mouse_handler(int byte)
{
    static uint8_t cycle = 0;
    static uint8_t mouse_data[3];
    mouse_data[cycle++] = byte & 0xFF;

    if (cycle == 1 && !(mouse_data[0] & 0x8))
        cycle = 0;

    if (cycle == 3) {
        cycle = 0;  /* We are done .. reset */
        struct mouse_packet *packet = (struct mouse_packet *) mouse_data;

        if (packet->x_over || packet->y_over)
            return;

        ringbuf_write(mouse_ring, 3, (char *) mouse_data);
        
        if (mouse_read_queue->count)
            thread_queue_wakeup(mouse_read_queue);
    }
}

void ps2mouse_register()
{
    x86_i8042_handler_register(2, ps2mouse_handler);
}

static ssize_t ps2mouse_read(struct devid *dd __unused, off_t offset __unused, size_t size, void *buf)
{
    return ringbuf_read(mouse_ring, size, buf);
}

static void __mouse_wait(uint32_t i)
{
    return;
    //printk("__mouse_wait(%d)\n", i);
    if (!i)
        while (!(io_in8(&mouse, MOUSE_CMD) & 1));
    else
        while (io_in8(&mouse, MOUSE_CMD) & 2);
}

static uint8_t __mouse_read()
{
    __mouse_wait(0);
    return io_in8(&mouse, MOUSE_DATA);
}

static void __mouse_write(uint8_t chr)
{
    __mouse_wait(1);
    io_out8(&mouse, MOUSE_CMD, SEND_CMD);
    __mouse_wait(1);
    io_out8(&mouse, MOUSE_DATA, chr);
}

int ps2mouse_probe()
{
    __mouse_wait(1);
    io_out8(&mouse, MOUSE_CMD, ENABLE_AUX_PS2);

    __mouse_wait(1);
    io_out8(&mouse, MOUSE_CMD, GET_COMPAQ_STATUS);
    uint8_t status = __mouse_read() | 2;

    __mouse_wait(1);
    io_out8(&mouse, MOUSE_CMD, SET_COMPAQ_STATUS);

    __mouse_wait(0);
    io_out8(&mouse, MOUSE_DATA, status);

    __mouse_write(USE_DEFAULTS);
    __mouse_read();

    __mouse_write(ENABLE_MOUSE);
    __mouse_read();

    ps2mouse_register();
    kdev_chrdev_register(10, &ps2mousedev);

    return 0;
}

/* File Operations */

static int ps2mouse_file_open(struct file *file)
{
    if (proc) /* Only one process can open kbd */
        return -EACCES;

    proc = cur_thread->owner;
    file->node->read_queue = mouse_read_queue;    

    return posix_file_open(file);
}

static int ps2mouse_file_close(struct file *file)
{
    proc = NULL;
    return posix_file_close(file);
}

struct dev ps2mousedev = {
    .name  = "mousedev",

    .probe = ps2mouse_probe,
    .read  = ps2mouse_read,

    .fops  = {
        .open  = ps2mouse_file_open,
        .read  = posix_file_read,
        .close = ps2mouse_file_close,

        .can_read  = __always,
        .can_write = __never,
        .eof       = __never,
    },
};

MODULE_INIT(ps2mouse, ps2mouse_probe, NULL)
