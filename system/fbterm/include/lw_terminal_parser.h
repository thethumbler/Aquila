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

#ifndef __TERMINAL_H__
#define __TERMINAL_H__

/*
**
** Introduction
** ============
**
** terminal.c is a basic level that parses escape sequences to call
** appropriated callbacks permiting you to implement a specific terminal.
**
** Callbacks
** =========
**
** terminal.c maps sequences to callbacks in this way :
** \033...  maps to terminal->callbacks->esc
** \033[... maps to terminal->callbacks->csi
** \033#... maps to terminal->callbacks->hash
** and \033( and \033) maps to terminal->callbacks->scs
**
** In 'callbacks', esc, csi, hash and scs are structs ascii_callbacks
** where you can bind your callbacks.
**
** Typically when terminal parses \033[42;43m
** it calls terminal->callbacks->csi->m(terminal);
**
** Parameters (here 42;43) are stored in terminal->argc and terminal->argv
** argv is an array of integers of length argc.
**
** Public members
** ==============
**
** void *user_data :
**     A (void *) where your implementation can store whatever you want
**     to get it back on your callabks.
**
** void (*write)(struct terminal *, char c) :
**    Hook for your implementation to recieve chars that are not
**    escape sequences
**
** struct term_callbacks callbacks :
**    Hooks for your callbacks to recieve escape sequences
**
** enum term_state state :
**     During a callback, typically a scs, you can read here if it's a
**     G1SET or a G0SET
**
** unsigned int argc :
**     For your callbacks, to know how many parameters are available
**     in argv.
**
** unsigned int argv[TERM_STACK_SIZE] :
**     For your callbacks, parameters of escape sequences are accessible
**     here.
**     \033[42;43m will have 2 in argc and argv[0] = 42, argv[1] = 43
**
** char flag;
**     Optinal constructor flag present before parameters, like in :
**     \033[?1049h -> The flag will be '?'
**     Otherwise the flag is set to '\0'
**
** void (*unimplemented)(struct terminal*, char *seq, char chr) :
**     Can be NULL, you can hook here to know where the terminal parses an
**     escape sequence on which you have not registered a callback.
**
** Exemple
** =======
**
** See terminal_vt100.c for a working exemple.
**
*/

#define TERM_STACK_SIZE 1024

enum term_state
{
    INIT,
    ESC,
    HASH,
    G0SET,
    G1SET,
    CSI
};

struct lw_terminal;

typedef void (*term_action)(struct lw_terminal *emul);

struct ascii_callbacks
{
    term_action n0;
    term_action n1;
    term_action n2;
    term_action n3;
    term_action n4;
    term_action n5;
    term_action n6;
    term_action n7;
    term_action n8;
    term_action n9;

    term_action h3A;
    term_action h3B;
    term_action h3C;
    term_action h3D;
    term_action h3E;
    term_action h3F;
    term_action h40;

    term_action A;
    term_action B;
    term_action C;
    term_action D;
    term_action E;
    term_action F;
    term_action G;
    term_action H;
    term_action I;
    term_action J;
    term_action K;
    term_action L;
    term_action M;
    term_action N;
    term_action O;
    term_action P;
    term_action Q;
    term_action R;
    term_action S;
    term_action T;
    term_action U;
    term_action V;
    term_action W;
    term_action X;
    term_action Y;
    term_action Z;

    term_action h5B;
    term_action h5C;
    term_action h5D;
    term_action h5E;
    term_action h5F;
    term_action h60;

    term_action a;
    term_action b;
    term_action c;
    term_action d;
    term_action e;
    term_action f;
    term_action g;
    term_action h;
    term_action i;
    term_action j;
    term_action k;
    term_action l;
    term_action m;
    term_action n;
    term_action o;
    term_action p;
    term_action q;
    term_action r;
    term_action s;
    term_action t;
    term_action u;
    term_action v;
    term_action w;
    term_action x;
    term_action y;
    term_action z;
};

struct term_callbacks
{
    struct ascii_callbacks esc;
    struct ascii_callbacks csi;
    struct ascii_callbacks hash;
    struct ascii_callbacks scs;
};

struct lw_terminal
{
    unsigned int           cursor_pos_x;
    unsigned int           cursor_pos_y;
    enum term_state        state;
    unsigned int           argc;
    unsigned int           argv[TERM_STACK_SIZE];
    void                   (*write)(struct lw_terminal *, char c);
    char                   stack[TERM_STACK_SIZE];
    unsigned int           stack_ptr;
    struct term_callbacks  callbacks;
    char                   flag;
    void                   *user_data;
    void                   (*unimplemented)(struct lw_terminal*,
                                            char *seq, char chr);
};

struct lw_terminal *lw_terminal_parser_init(void);
void lw_terminal_parser_default_unimplemented(struct lw_terminal* this, char *seq, char chr);
void lw_terminal_parser_read(struct lw_terminal *this, char c);
void lw_terminal_parser_read_str(struct lw_terminal *this, char *c);
void lw_terminal_parser_destroy(struct lw_terminal* this);
#endif
