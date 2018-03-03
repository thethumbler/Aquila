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

#define _XOPEN_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <termios.h>
#include <hl_vt100.h>

struct vt100_headless *new_vt100_headless(void)
{
    return calloc(1, sizeof(struct vt100_headless));
}

void delete_vt100_headless(struct vt100_headless *this)
{
    free(this);
}

static void set_non_canonical(struct vt100_headless *this, int fd)
{
    struct termios termios;

    ioctl(fd, TCGETS, &this->backup);
    ioctl(fd, TCGETS, &termios);
    termios.c_iflag |= ICANON;
    termios.c_cc[VMIN] = 1;
    termios.c_cc[VTIME] = 0;
    ioctl(fd, TCSETS, &termios);
}

static void restore_termios(struct vt100_headless *this, int fd)
{
    ioctl(fd, TCSETS, &this->backup);
}

#ifndef NDEBUG
static void strdump(char *str)
{
    while (*str != '\0')
    {
        if (*str >= ' ' && *str <= '~')
            fprintf(stderr, "%c", *str);
        else
            fprintf(stderr, "\\0%o", *str);
        str += 1;
    }
    fprintf(stderr, "\n");
}
#endif

void vt100_headless_stop(struct vt100_headless *this)
{
    this->should_quit = 1;
}

int vt100_headless_main_loop(struct vt100_headless *this)
{
    char buffer[4096];
    int retval;
    ssize_t read_size;

    while (!this->should_quit)
    {
        memset(buffer, 0, 4096);
        if ((read_size = read(0, buffer, 50)) > 0) {
            buffer[read_size] = '\0';
            lw_terminal_vt100_read_str(this->term, buffer);

            if (this->changed != NULL)
                this->changed(this);

            //fbterm_write(&term[0], buf, 50);
            //fbterm_redraw(&term[0]);
        }
//        FD_ZERO(&rfds);
//        FD_SET(this->master, &rfds);
//        FD_SET(0, &rfds);
//        //retval = select(this->master + 1, &rfds, NULL, NULL, NULL);
//        if (retval == -1)
//        {
//            perror("select()");
//        }
//        if (FD_ISSET(0, &rfds))
//        {
//            read_size = read(0, &buffer, 4096);
//            if (read_size == -1)
//            {
//                perror("read");
//                return EXIT_FAILURE;
//            }
//            buffer[read_size] = '\0';
//            write(this->master, buffer, read_size);
//        }
//        if (FD_ISSET(this->master, &rfds))
//        {
//            read_size = read(this->master, &buffer, 4096);
//            if (read_size == -1)
//            {
//                perror("read");
//                return EXIT_FAILURE;
//            }
//            buffer[read_size] = '\0';
//#ifndef NDEBUG
//            strdump(buffer);
//#endif
//            lw_terminal_vt100_read_str(this->term, buffer);
//            if (this->changed != NULL)
//                this->changed(this);
//        }
    }

    return EXIT_SUCCESS;
}

void master_write(void *user_data, void *buffer, size_t len)
{
    struct vt100_headless *this;

    this = (struct vt100_headless*)user_data;
    write(this->master, buffer, len);
}

const char **vt100_headless_getlines(struct vt100_headless *this)
{
    return lw_terminal_vt100_getlines(this->term);
}

void vt100_headless_fork(struct vt100_headless *this,
                         const char *progname,
                         char **argv)
{
    //int child;
    //struct winsize winsize;

    //set_non_canonical(this, 0);
    //winsize.ws_row = 24;
    //winsize.ws_col = 80;
    //child = forkpty(&this->master, NULL, NULL, NULL);
    //if (child == CHILD)
    //{
    //    setsid();
    //    putenv("TERM=vt100");
    //    execvp(progname, argv);
    //    return ;
    //}
    //else
    //{
    //    this->term = lw_terminal_vt100_init(this, lw_terminal_parser_default_unimplemented);
    //    this->term->master_write = master_write;
    //    //ioctl(this->master, TIOCSWINSZ, &winsize);
    //}
    //restore_termios(this, 0);
}
