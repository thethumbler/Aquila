/*
 * Copyright (c) 2016 Julien Palard.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __VT100_HEADLESS_H__
#define __VT100_HEADLESS_H__

#define CHILD 0

#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#include <lw_terminal_vt100.h>

struct vt100_headless
{
    int master;
    struct lw_terminal_vt100 *term;
    struct termios backup;
    int should_quit;
    void (*changed)(struct vt100_headless *this);
};


void vt100_headless_fork(struct vt100_headless *this, const char *progname, char **argv);
int vt100_headless_main_loop(struct vt100_headless *this);
void delete_vt100_headless(struct vt100_headless *this);
struct vt100_headless *new_vt100_headless(void);
const char **vt100_headless_getlines(struct vt100_headless *this);
void vt100_headless_stop(struct vt100_headless *this);

#endif
