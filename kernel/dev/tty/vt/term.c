/*
 * Terminal emulator of the virtual terminals: what the Linux console does
 * with what programs write (TERM=linux), and the parts of xterm programs
 * commonly use (alternate screen, 256 colors). Characters are shown with
 * the 256 glyphs of code page 437, UTF-8 is decoded to those.
 */

#include <core/system.h>
#include <core/string.h>
#include <mm/kvmem.h>
#include <dev/vt.h>

MALLOC_DEFINE(M_VT_SCREEN, "vt-screen", "virtual terminal screen");

enum {
    S_GROUND,
    S_ESC,
    S_CHARSET,      /* ESC ( or ESC ), the G0 or G1 set follows */
    S_HASH,         /* ESC # */
    S_PERCENT,      /* ESC % */
    S_CSI,
    S_OSC,
    S_PALETTE,      /* ESC ] P, 7 hex digits follow */
    S_STRING,       /* DCS, SOS, PM, APC: ignored up to ST */
    S_STRING_ESC,   /* ESC in a string, ST if \ follows */
};

#define DEFAULT_FG  7
#define DEFAULT_BG  0

/* DEC special graphics, 0x5f to 0x7e */
static const uint8_t dec_graphics[] = {
    ' ',  0x04, 0xB1, 0xF9, 0xF9, 0xF9, 0xF9, 0xF8,     /* _ ` a b c d e f */
    0xF1, 0xB0, 0x0F, 0xD9, 0xBF, 0xDA, 0xC0, 0xC5,     /* g h i j k l m n */
    0xC4, 0xC4, 0xC4, 0xC4, 0xC4, 0xC3, 0xB4, 0xC1,     /* o p q r s t u v */
    0xC2, 0xB3, 0xF3, 0xF2, 0xE3, '!',  0x9C, 0xFA,     /* w x y z { | } ~ */
};

/* code page 437 0x80 to 0xff */
static const uint16_t cp437[128] = {
    0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7,
    0x00EA, 0x00EB, 0x00E8, 0x00EF, 0x00EE, 0x00EC, 0x00C4, 0x00C5,
    0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2, 0x00FB, 0x00F9,
    0x00FF, 0x00D6, 0x00DC, 0x00A2, 0x00A3, 0x00A5, 0x20A7, 0x0192,
    0x00E1, 0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1, 0x00AA, 0x00BA,
    0x00BF, 0x2310, 0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB,
    0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556,
    0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510,
    0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F,
    0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x2567,
    0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B,
    0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580,
    0x03B1, 0x00DF, 0x0393, 0x03C0, 0x03A3, 0x03C3, 0x00B5, 0x03C4,
    0x03A6, 0x0398, 0x03A9, 0x03B4, 0x221E, 0x03C6, 0x03B5, 0x2229,
    0x2261, 0x00B1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00F7, 0x2248,
    0x00B0, 0x2219, 0x00B7, 0x221A, 0x207F, 0x00B2, 0x25A0, 0x00A0,
};

/* the symbols of code page 437 0x01 to 0x1f, and 0x7f */
static const uint16_t cp437_symbols[32] = {
    0x2302, 0x263A, 0x263B, 0x2665, 0x2666, 0x2663, 0x2660, 0x2022,
    0x25D8, 0x25CB, 0x25D9, 0x2642, 0x2640, 0x266A, 0x266B, 0x263C,
    0x25BA, 0x25C4, 0x2195, 0x203C, 0x00B6, 0x00A7, 0x25AC, 0x21A8,
    0x2191, 0x2193, 0x2192, 0x2190, 0x221F, 0x2194, 0x25B2, 0x25BC,
};

/* other characters with a glyph close enough */
static const struct { uint16_t u; uint8_t ch; } unicode_extra[] = {
    {0x25C6, 0x04},
    {0x256D, 0xDA}, {0x256E, 0xBF}, {0x256F, 0xD9}, {0x2570, 0xC0},
    {0x2018, '\''}, {0x2019, '\''}, {0x201C, '"'},  {0x201D, '"'},
    {0x2010, '-'},  {0x2013, '-'},  {0x2014, '-'},  {0x2212, '-'},
    {0x2026, 0xFA}, {0x00B4, '\''}, {0x00D7, 'x'},
};

static uint8_t unicode_glyph(uint32_t u)
{
    if (u < 0x80)
        return u;

    for (int i = 0; i < 128; ++i)
        if (cp437[i] == u)
            return 0x80 + i;

    for (int i = 0; i < 32; ++i)
        if (cp437_symbols[i] == u)
            return i ? i : 0x7f;

    for (size_t i = 0; i < sizeof(unicode_extra) / sizeof(unicode_extra[0]); ++i)
        if (unicode_extra[i].u == u)
            return unicode_extra[i].ch;

    return '?';
}

/* the closest color of the 6x6x6 cube or the gray ramp of the palette */
static uint8_t rgb_color(int r, int g, int b)
{
    int ri = r < 48 ? 0 : r < 115 ? 1 : (r - 35) / 40;
    int gi = g < 48 ? 0 : g < 115 ? 1 : (g - 35) / 40;
    int bi = b < 48 ? 0 : b < 115 ? 1 : (b - 35) / 40;

    if (ri == gi && gi == bi) {
        int gray = (r + g + b) / 3;

        if (gray > 8 && gray < 238)
            return 232 + (gray - 8) / 10;
    }

    return 16 + 36 * ri + 6 * gi + bi;
}

static inline struct vt_cell *cell(struct vt *vt, int row, int col)
{
    return &vt->screen[row * vt->cols + col];
}

/* erased cells take the current colors (bce) */
static inline struct vt_cell blank(struct vt *vt)
{
    return (struct vt_cell) {.ch = ' ', .fg = vt->pen.fg, .bg = vt->pen.bg};
}

static void erase(struct vt *vt, int row, int from, int to)
{
    struct vt_cell b = blank(vt);

    for (int col = from; col < to; ++col)
        *cell(vt, row, col) = b;
}

/* moves rows top+n..bottom up n rows, n blank rows at the bottom */
static void scroll_up(struct vt *vt, int top, int bottom, int n)
{
    if (n > bottom - top + 1)
        n = bottom - top + 1;

    memmove(cell(vt, top, 0), cell(vt, top + n, 0),
            (bottom - top + 1 - n) * vt->cols * sizeof(struct vt_cell));

    for (int row = bottom - n + 1; row <= bottom; ++row)
        erase(vt, row, 0, vt->cols);
}

static void scroll_down(struct vt *vt, int top, int bottom, int n)
{
    if (n > bottom - top + 1)
        n = bottom - top + 1;

    memmove(cell(vt, top + n, 0), cell(vt, top, 0),
            (bottom - top + 1 - n) * vt->cols * sizeof(struct vt_cell));

    for (int row = top; row < top + n; ++row)
        erase(vt, row, 0, vt->cols);
}

/* cursor movement, within the scrolling region in origin mode */
static void move_to(struct vt *vt, int row, int col)
{
    int min = vt->origin ? vt->top : 0;
    int max = vt->origin ? vt->bottom : vt->rows - 1;

    vt->row = row < min ? min : row > max ? max : row;
    vt->col = col < 0 ? 0 : col >= vt->cols ? vt->cols - 1 : col;
    vt->wrap_pending = 0;
}

/* vertical movement stops at the margins, if it starts within them */
static void move_rows(struct vt *vt, int n)
{
    int row = vt->row + n;

    if (n < 0 && vt->row >= vt->top && row < vt->top)
        row = vt->top;
    if (n > 0 && vt->row <= vt->bottom && row > vt->bottom)
        row = vt->bottom;

    row = row < 0 ? 0 : row >= vt->rows ? vt->rows - 1 : row;

    vt->row = row;
    vt->wrap_pending = 0;
}

static void linefeed(struct vt *vt)
{
    if (vt->row == vt->bottom)
        scroll_up(vt, vt->top, vt->bottom, 1);
    else if (vt->row < vt->rows - 1)
        vt->row++;

    vt->wrap_pending = 0;
}

static void reverse_index(struct vt *vt)
{
    if (vt->row == vt->top)
        scroll_down(vt, vt->top, vt->bottom, 1);
    else if (vt->row > 0)
        vt->row--;

    vt->wrap_pending = 0;
}

static int tab_stop(struct vt *vt, int col)
{
    return col < 256 && (vt->tabs[col / 8] & (1 << (col % 8)));
}

static void tab_set(struct vt *vt, int col, int set)
{
    if (col >= 256)
        return;

    if (set)
        vt->tabs[col / 8] |= 1 << (col % 8);
    else
        vt->tabs[col / 8] &= ~(1 << (col % 8));
}

static void tab(struct vt *vt, int n)
{
    while (n > 0 && vt->col < vt->cols - 1) {
        vt->col++;
        if (tab_stop(vt, vt->col))
            --n;
    }

    while (n < 0 && vt->col > 0) {
        vt->col--;
        if (tab_stop(vt, vt->col))
            ++n;
    }

    vt->wrap_pending = 0;
}

static void put_glyph(struct vt *vt, uint8_t ch)
{
    if (vt->wrap_pending && vt->autowrap) {
        vt->col = 0;
        linefeed(vt);
    }

    struct vt_cell *c = cell(vt, vt->row, vt->col);

    if (vt->insert)
        memmove(c + 1, c, (vt->cols - vt->col - 1) * sizeof(struct vt_cell));

    *c = vt->pen;
    c->ch = ch;
    vt->last = ch;

    if (vt->col < vt->cols - 1)
        vt->col++;
    else
        vt->wrap_pending = vt->autowrap;
}

/* a character, through the character set in use */
static void put_char(struct vt *vt, uint32_t u)
{
    if (vt->charset[vt->gl] == '0') {
        if (u >= 0x5f && u <= 0x7e) {
            put_glyph(vt, dec_graphics[u - 0x5f]);
            return;
        }

        /* arrows and a block, as on the Linux console */
        switch (u) {
            case '+': put_glyph(vt, 0x1A); return;
            case ',': put_glyph(vt, 0x1B); return;
            case '-': put_glyph(vt, 0x18); return;
            case '.': put_glyph(vt, 0x19); return;
            case '0': put_glyph(vt, 0xDB); return;
        }
    }

    put_glyph(vt, unicode_glyph(u));
}

static void reply(struct vt *vt, const char *s)
{
    size_t len = strlen(s);

    if (vt->reply_len + len > sizeof(vt->reply))
        return;

    memcpy(vt->reply + vt->reply_len, s, len);
    vt->reply_len += len;
}

static void save_cursor(struct vt *vt, struct vt_cursor *save)
{
    save->row = vt->row;
    save->col = vt->col;
    save->pen = vt->pen;
    save->charset[0] = vt->charset[0];
    save->charset[1] = vt->charset[1];
    save->gl = vt->gl;
    save->origin = vt->origin;
    save->wrap = vt->autowrap;
}

static void restore_cursor(struct vt *vt, struct vt_cursor *save)
{
    vt->pen = save->pen;
    vt->charset[0] = save->charset[0];
    vt->charset[1] = save->charset[1];
    vt->gl = save->gl;
    vt->origin = save->origin;
    vt->autowrap = save->wrap;

    vt->row = save->row >= vt->rows ? vt->rows - 1 : save->row;
    vt->col = save->col >= vt->cols ? vt->cols - 1 : save->col;
    vt->wrap_pending = 0;
}

static void clear_screen(struct vt *vt)
{
    for (int row = 0; row < vt->rows; ++row)
        erase(vt, row, 0, vt->cols);
}

/* the alternate screen (xterm), cleared when it is shown */
static void alt_screen(struct vt *vt, int on)
{
    if (on && vt->screen != vt->alt) {
        vt->screen = vt->alt;
        clear_screen(vt);
    } else if (!on && vt->screen == vt->alt) {
        vt->screen = vt->main;
    }
}

/* modes and pen to the defaults, not the screen (DECSTR) */
static void soft_reset(struct vt *vt)
{
    vt->pen = (struct vt_cell) {.ch = ' ', .fg = DEFAULT_FG, .bg = DEFAULT_BG};
    vt->top = 0;
    vt->bottom = vt->rows - 1;
    vt->autowrap = 1;
    vt->origin = 0;
    vt->insert = 0;
    vt->newline = 0;
    vt->cursor_visible = 1;
    vt->cursor_keys = 0;
    vt->keypad = 0;
    /* G1 is the line drawing set, as on the Linux console */
    vt->charset[0] = 'B';
    vt->charset[1] = '0';
    vt->gl = 0;
    vt->wrap_pending = 0;

    save_cursor(vt, &vt->saved);
    vt->saved.row = vt->saved.col = 0;
    vt->saved_alt = vt->saved;
}

void vt_term_reset(struct vt *vt)
{
    vt->screen = vt->main;
    soft_reset(vt);
    vt->reverse_screen = 0;

    for (int col = 0; col < 256; ++col)
        tab_set(vt, col, col && col % 8 == 0);

    clear_screen(vt);
    vt->row = vt->col = 0;
    vt->state = S_GROUND;
    vt->utf8_left = 0;
    vt->last = 0;
}

int vt_term_init(struct vt *vt, int rows, int cols)
{
    size_t size = rows * cols * sizeof(struct vt_cell);

    vt->main = kmalloc(size, &M_VT_SCREEN, 0);
    vt->alt  = kmalloc(size, &M_VT_SCREEN, 0);

    if (!vt->main || !vt->alt) {
        if (vt->main) kfree(vt->main);
        if (vt->alt)  kfree(vt->alt);
        vt->main = vt->alt = NULL;
        return -ENOMEM;
    }

    vt->rows = rows;
    vt->cols = cols;

    /* both cleared */
    vt->screen = vt->alt;
    vt->pen = (struct vt_cell) {.ch = ' ', .fg = DEFAULT_FG, .bg = DEFAULT_BG};
    clear_screen(vt);
    vt_term_reset(vt);

    return 0;
}

/* a parameter, 0 or a missing one is the default */
static int param(struct vt *vt, int i, int def)
{
    return i < vt->nparams && vt->params[i] ? vt->params[i] : def;
}

static void sgr(struct vt *vt)
{
    struct vt_cell *pen = &vt->pen;

    for (int i = 0; i < vt->nparams; ++i) {
        int p = vt->params[i];

        switch (p) {
            case 0:
                pen->attr = 0;
                pen->fg = DEFAULT_FG;
                pen->bg = DEFAULT_BG;
                break;
            case 1: pen->attr |= VT_BOLD; break;
            case 2: pen->attr |= VT_DIM; break;
            case 3: pen->attr |= VT_ITALIC; break;
            case 4: pen->attr |= VT_UNDERLINE; break;
            case 5: pen->attr |= VT_BLINK; break;
            case 7: pen->attr |= VT_REVERSE; break;
            case 8: pen->attr |= VT_INVISIBLE; break;
            case 21:
            case 22: pen->attr &= ~(VT_BOLD | VT_DIM); break;
            case 23: pen->attr &= ~VT_ITALIC; break;
            case 24: pen->attr &= ~VT_UNDERLINE; break;
            case 25: pen->attr &= ~VT_BLINK; break;
            case 27: pen->attr &= ~VT_REVERSE; break;
            case 28: pen->attr &= ~VT_INVISIBLE; break;
            case 39: pen->fg = DEFAULT_FG; break;
            case 49: pen->bg = DEFAULT_BG; break;
            case 38:
            case 48: {
                /* 38;5;n or 38;2;r;g;b */
                int color = -1;

                if (i + 2 < vt->nparams && vt->params[i + 1] == 5) {
                    color = vt->params[i + 2] & 0xff;
                    i += 2;
                } else if (i + 4 < vt->nparams && vt->params[i + 1] == 2) {
                    color = rgb_color(vt->params[i + 2], vt->params[i + 3], vt->params[i + 4]);
                    i += 4;
                } else {
                    i = vt->nparams;
                }

                if (color >= 0) {
                    if (p == 38)
                        pen->fg = color;
                    else
                        pen->bg = color;
                }
                break;
            }
            default:
                if (p >= 30 && p <= 37)
                    pen->fg = p - 30;
                else if (p >= 40 && p <= 47)
                    pen->bg = p - 40;
                else if (p >= 90 && p <= 97)
                    pen->fg = p - 90 + 8;
                else if (p >= 100 && p <= 107)
                    pen->bg = p - 100 + 8;
        }
    }
}

/* DEC private modes, CSI ? n h and CSI ? n l */
static void decset(struct vt *vt, int set)
{
    for (int i = 0; i < vt->nparams; ++i) {
        switch (vt->params[i]) {
            case 1: vt->cursor_keys = set; break;
            case 5: vt->reverse_screen = set; break;
            case 6:
                vt->origin = set;
                move_to(vt, vt->origin ? vt->top : 0, 0);
                break;
            case 7: vt->autowrap = set; break;
            case 25: vt->cursor_visible = set; break;
            case 47:
            case 1047:
                alt_screen(vt, set);
                break;
            case 1048:
                if (set)
                    save_cursor(vt, &vt->saved);
                else
                    restore_cursor(vt, &vt->saved);
                break;
            case 1049:
                if (set) {
                    save_cursor(vt, &vt->saved_alt);
                    alt_screen(vt, 1);
                } else {
                    alt_screen(vt, 0);
                    restore_cursor(vt, &vt->saved_alt);
                }
                break;
        }
    }
}

static void csi(struct vt *vt, char f)
{
    int n = param(vt, 0, 1);
    char buf[24];

    if (vt->intermediate) {
        /* CSI ! p (DECSTR), others (cursor style CSI SP q) are ignored */
        if (vt->intermediate == '!' && f == 'p')
            soft_reset(vt);
        return;
    }

    if (vt->private == '?') {
        if (f == 'h' || f == 'l')
            decset(vt, f == 'h');
        return;
    }

    if (vt->private)
        return;

    switch (f) {
        case '@': { /* ICH */
            int count = MIN(n, vt->cols - vt->col);
            struct vt_cell *c = cell(vt, vt->row, vt->col);

            memmove(c + count, c, (vt->cols - vt->col - count) * sizeof(struct vt_cell));
            erase(vt, vt->row, vt->col, vt->col + count);
            vt->wrap_pending = 0;
            break;
        }
        case 'A': move_rows(vt, -n); break;
        case 'B':
        case 'e': move_rows(vt, n); break;
        case 'C':
        case 'a': vt->col = MIN(vt->col + n, vt->cols - 1); vt->wrap_pending = 0; break;
        case 'D': vt->col = MAX(vt->col - n, 0); vt->wrap_pending = 0; break;
        case 'E': move_rows(vt, n); vt->col = 0; break;
        case 'F': move_rows(vt, -n); vt->col = 0; break;
        case 'G':
        case '`': vt->col = MIN(n, vt->cols) - 1; vt->wrap_pending = 0; break;
        case 'H':
        case 'f':
            move_to(vt, (vt->origin ? vt->top : 0) + param(vt, 0, 1) - 1, param(vt, 1, 1) - 1);
            break;
        case 'I': tab(vt, n); break;
        case 'Z': tab(vt, -n); break;
        case 'J':
            switch (param(vt, 0, 0)) {
                case 0:
                    erase(vt, vt->row, vt->col, vt->cols);
                    for (int row = vt->row + 1; row < vt->rows; ++row)
                        erase(vt, row, 0, vt->cols);
                    break;
                case 1:
                    for (int row = 0; row < vt->row; ++row)
                        erase(vt, row, 0, vt->cols);
                    erase(vt, vt->row, 0, vt->col + 1);
                    break;
                case 2:
                    clear_screen(vt);
                    break;
            }
            break;
        case 'K':
            switch (param(vt, 0, 0)) {
                case 0: erase(vt, vt->row, vt->col, vt->cols); break;
                case 1: erase(vt, vt->row, 0, vt->col + 1); break;
                case 2: erase(vt, vt->row, 0, vt->cols); break;
            }
            break;
        case 'L': /* IL */
            if (vt->row >= vt->top && vt->row <= vt->bottom) {
                scroll_down(vt, vt->row, vt->bottom, n);
                vt->col = 0;
                vt->wrap_pending = 0;
            }
            break;
        case 'M': /* DL */
            if (vt->row >= vt->top && vt->row <= vt->bottom) {
                scroll_up(vt, vt->row, vt->bottom, n);
                vt->col = 0;
                vt->wrap_pending = 0;
            }
            break;
        case 'P': { /* DCH */
            int count = MIN(n, vt->cols - vt->col);
            struct vt_cell *c = cell(vt, vt->row, vt->col);

            memmove(c, c + count, (vt->cols - vt->col - count) * sizeof(struct vt_cell));
            erase(vt, vt->row, vt->cols - count, vt->cols);
            vt->wrap_pending = 0;
            break;
        }
        case 'X': /* ECH */
            erase(vt, vt->row, vt->col, MIN(vt->col + n, vt->cols));
            vt->wrap_pending = 0;
            break;
        case 'b': /* REP */
            if (vt->last)
                while (n--)
                    put_glyph(vt, vt->last);
            break;
        case 'c': /* DA: a VT102, like the Linux console */
            if (!param(vt, 0, 0))
                reply(vt, "\033[?6c");
            break;
        case 'd': /* VPA */
            move_to(vt, (vt->origin ? vt->top : 0) + n - 1, vt->col);
            break;
        case 'g': /* TBC */
            if (param(vt, 0, 0) == 0)
                tab_set(vt, vt->col, 0);
            else if (param(vt, 0, 0) == 3)
                for (int col = 0; col < 256; ++col)
                    tab_set(vt, col, 0);
            break;
        case 'h':
        case 'l':
            for (int i = 0; i < vt->nparams; ++i) {
                if (vt->params[i] == 4)
                    vt->insert = f == 'h';
                else if (vt->params[i] == 20)
                    vt->newline = f == 'h';
            }
            break;
        case 'm':
            sgr(vt);
            break;
        case 'n': /* DSR */
            if (param(vt, 0, 0) == 5) {
                reply(vt, "\033[0n");
            } else if (param(vt, 0, 0) == 6) {
                snprintf(buf, sizeof(buf), "\033[%d;%dR",
                        vt->row - (vt->origin ? vt->top : 0) + 1, vt->col + 1);
                reply(vt, buf);
            }
            break;
        case 'r': { /* DECSTBM */
            int top = param(vt, 0, 1) - 1;
            int bottom = param(vt, 1, vt->rows) - 1;

            if (bottom >= vt->rows)
                bottom = vt->rows - 1;

            if (top < bottom) {
                vt->top = top;
                vt->bottom = bottom;
                move_to(vt, vt->origin ? vt->top : 0, 0);
            }
            break;
        }
        case 's':
            save_cursor(vt, &vt->saved);
            break;
        case 'u':
            restore_cursor(vt, &vt->saved);
            break;
    }
}

static void esc(struct vt *vt, char c)
{
    vt->state = S_GROUND;

    switch (c) {
        case '[':
            vt->state = S_CSI;
            vt->params[0] = 0;
            vt->nparams = 1;
            vt->private = 0;
            vt->intermediate = 0;
            break;
        case ']':
            vt->state = S_OSC;
            vt->nparams = 0;    /* characters of the string so far */
            break;
        case 'P':
        case 'X':
        case '^':
        case '_':
            vt->state = S_STRING;
            break;
        case '(':
        case ')':
        case '*':
        case '+':
            vt->state = S_CHARSET;
            vt->intermediate = c;
            break;
        case '#':
            vt->state = S_HASH;
            break;
        case '%':
            vt->state = S_PERCENT;
            break;
        case '7': save_cursor(vt, &vt->saved); break;
        case '8': restore_cursor(vt, &vt->saved); break;
        case 'D': linefeed(vt); break;
        case 'E': vt->col = 0; linefeed(vt); break;
        case 'H': tab_set(vt, vt->col, 1); break;
        case 'M': reverse_index(vt); break;
        case 'Z': reply(vt, "\033[?6c"); break;
        case 'c': vt_term_reset(vt); break;
        case '=': vt->keypad = 1; break;
        case '>': vt->keypad = 0; break;
    }
}

static void control(struct vt *vt, char c)
{
    switch (c) {
        case '\b':
            if (vt->col > 0)
                vt->col--;
            vt->wrap_pending = 0;
            break;
        case '\t':
            tab(vt, 1);
            break;
        case '\n':
        case '\v':
        case '\f':
            if (vt->newline)
                vt->col = 0;
            linefeed(vt);
            break;
        case '\r':
            vt->col = 0;
            vt->wrap_pending = 0;
            break;
        case 0x0e:  /* SO */
            vt->gl = 1;
            break;
        case 0x0f:  /* SI */
            vt->gl = 0;
            break;
        case 0x18:  /* CAN */
        case 0x1a:  /* SUB */
            vt->state = S_GROUND;
            break;
        case 0x1b:
            vt->state = S_ESC;
            break;
    }
}

static void utf8(struct vt *vt, uint8_t c)
{
    if (c < 0x80) {
        vt->utf8_left = 0;
        put_char(vt, c);
    } else if ((c & 0xc0) == 0x80) {
        if (!vt->utf8_left) {
            put_glyph(vt, '?');
            return;
        }

        vt->utf8 = (vt->utf8 << 6) | (c & 0x3f);

        /* C1 controls aren't shown */
        if (!--vt->utf8_left && (vt->utf8 < 0x80 || vt->utf8 > 0x9f))
            put_char(vt, vt->utf8);
    } else {
        if (vt->utf8_left)
            put_glyph(vt, '?');

        if ((c & 0xe0) == 0xc0) {
            vt->utf8 = c & 0x1f;
            vt->utf8_left = 1;
        } else if ((c & 0xf0) == 0xe0) {
            vt->utf8 = c & 0x0f;
            vt->utf8_left = 2;
        } else if ((c & 0xf8) == 0xf0) {
            vt->utf8 = c & 0x07;
            vt->utf8_left = 3;
        } else {
            vt->utf8_left = 0;
            put_glyph(vt, '?');
        }
    }
}

static int hexdigit(char c)
{
    return c >= '0' && c <= '9' ? c - '0' :
           c >= 'a' && c <= 'f' ? c - 'a' + 10 :
           c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

static void term_char(struct vt *vt, uint8_t c)
{
    switch (vt->state) {
        case S_OSC:
            /* the Linux palette sequences: ESC ] P nrrggbb, ESC ] R */
            if (!vt->nparams && c == 'P') {
                vt->state = S_PALETTE;
                return;
            }

            if (!vt->nparams && c == 'R') {
                vt->state = S_GROUND;
                return;
            }

            vt->nparams++;
            /* fall through */
        case S_STRING:
            if (c == 0x07 || c == 0x18 || c == 0x1a)
                vt->state = S_GROUND;
            else if (c == 0x1b)
                vt->state = S_STRING_ESC;
            return;
        case S_STRING_ESC:
            if (c == '\\') {
                vt->state = S_GROUND;
                return;
            }

            vt->state = S_ESC;
            break;
        case S_PALETTE:
            /* not supported, the digits are skipped */
            if (hexdigit(c) < 0 || ++vt->nparams == 7)
                vt->state = S_GROUND;
            return;
    }

    if (c < 0x20) {
        control(vt, c);
        return;
    }

    if (c == 0x7f)
        return;

    switch (vt->state) {
        case S_GROUND:
            utf8(vt, c);
            break;
        case S_ESC:
            esc(vt, c);
            break;
        case S_CHARSET:
            if (vt->intermediate == '(')
                vt->charset[0] = c;
            else if (vt->intermediate == ')')
                vt->charset[1] = c;
            vt->state = S_GROUND;
            break;
        case S_HASH:
            /* DECALN: the screen filled with E */
            if (c == '8') {
                for (int row = 0; row < vt->rows; ++row)
                    for (int col = 0; col < vt->cols; ++col)
                        *cell(vt, row, col) = (struct vt_cell) {.ch = 'E', .fg = DEFAULT_FG, .bg = DEFAULT_BG};
                move_to(vt, 0, 0);
            }
            vt->state = S_GROUND;
            break;
        case S_PERCENT:
            /* UTF-8 is always on */
            vt->state = S_GROUND;
            break;
        case S_CSI:
            if (c >= '0' && c <= '9') {
                int *p = &vt->params[vt->nparams - 1];
                if (*p < 10000)
                    *p = *p * 10 + c - '0';
            } else if (c == ';' || c == ':') {
                if (vt->nparams < (int) (sizeof(vt->params) / sizeof(vt->params[0])))
                    vt->params[vt->nparams++] = 0;
            } else if (c >= 0x3c && c <= 0x3f) {
                vt->private = c;
            } else if (c >= 0x20 && c <= 0x2f) {
                vt->intermediate = c;
            } else {
                vt->state = S_GROUND;
                if (c >= 0x40 && c <= 0x7e)
                    csi(vt, c);
            }
            break;
    }
}

void vt_term_write(struct vt *vt, const char *buf, size_t size)
{
    for (size_t i = 0; i < size; ++i)
        term_char(vt, buf[i]);
}
