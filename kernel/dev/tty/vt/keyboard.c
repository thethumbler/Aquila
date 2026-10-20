/*
 * The keyboard of the virtual terminals: PS/2 scancodes (set 1, what the
 * i8042 translates to) to what the Linux console sends for the keys, with
 * the US layout. Alt sends ESC before the key, Alt+F1 to Alt+F6 switch
 * terminals, Ctrl+Alt+Del restarts (or tells init, see sys/reboot.c).
 *
 * Or, if the terminal's program asked for them (KDSKBMODE), the scancodes
 * as they come (K_RAW), or Linux key codes, with 0x80 for releases
 * (K_MEDIUMRAW); the program handles every key then, switching too.
 */

#include <core/system.h>
#include <core/string.h>
#include <dev/vt.h>
#include <sys/reboot.h>
#include <aquila/keyboard.h>

/* scancodes */
#define SC_ESC      0x01
#define SC_BS       0x0E
#define SC_TAB      0x0F
#define SC_ENTER    0x1C
#define SC_CTRL     0x1D
#define SC_LSHIFT   0x2A
#define SC_RSHIFT   0x36
#define SC_KP_MUL   0x37
#define SC_ALT      0x38
#define SC_CAPS     0x3A
#define SC_F1       0x3B
#define SC_F10      0x44
#define SC_NUM      0x45
#define SC_KP7      0x47
#define SC_KP_DEL   0x53
#define SC_F11      0x57
#define SC_F12      0x58

static const char keys[128] = {
    [0x02] = '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=',
    [0x10] = 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']',
    [0x1E] = 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    [0x2B] = '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/',
    [0x39] = ' ',
    [0x56] = '<',
};

static const char shifted[128] = {
    [0x02] = '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+',
    [0x10] = 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}',
    [0x1E] = 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    [0x2B] = '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?',
    [0x39] = ' ',
    [0x56] = '>',
};

/* function keys, F1 to F12 */
static const char *fkeys[12] = {
    "\033[[A", "\033[[B", "\033[[C", "\033[[D", "\033[[E", "\033[17~",
    "\033[18~", "\033[19~", "\033[20~", "\033[21~", "\033[23~", "\033[24~",
};

/* keypad 7 to ., without num lock: the keys of the editing block, NULL
 * for - and + which are the same either way */
static const char *keypad_keys[13] = {
    "\033[1~", "A", "\033[5~", NULL, "D", "\033[G", "C", NULL,
    "\033[4~", "B", "\033[6~", "\033[2~", "\033[3~",
};

static const char keypad_digits[] = "789-456+1230.";

/* Linux key codes of the keys with an E0 prefix, the others' are their
 * scancodes */
static const uint8_t e0_keycodes[128] = {
    [0x1C] = 96,    /* keypad Enter */
    [0x1D] = 97,    /* right Ctrl */
    [0x35] = 98,    /* keypad / */
    [0x37] = 99,    /* Print Screen */
    [0x38] = 100,   /* right Alt */
    [0x47] = 102,   /* Home */
    [0x48] = 103,   /* Up */
    [0x49] = 104,   /* Page Up */
    [0x4B] = 105,   /* Left */
    [0x4D] = 106,   /* Right */
    [0x4F] = 107,   /* End */
    [0x50] = 108,   /* Down */
    [0x51] = 109,   /* Page Down */
    [0x52] = 110,   /* Insert */
    [0x53] = 111,   /* Delete */
    [0x5B] = 125,   /* left Windows */
    [0x5C] = 126,   /* right Windows */
    [0x5D] = 127,   /* Menu */
};

#define KEY_PAUSE   119

static int shift, ctrl, alt, caps, num;

/* a prefix (E0) for the next scancode, bytes of Pause (E1) to skip */
static int extended, skip;

static void send(const char *s)
{
    vt_input(s, strlen(s));
}

static void send_byte(int c)
{
    char b = c;
    vt_input(&b, 1);
}

/*
 * KDGKBENT: what a key (its Linux key code) is, in the table of Shift and
 * AltGr (the same as without: the US layout has no AltGr), for programs
 * that read key codes (K_MEDIUMRAW)
 */
int vt_keymap(int table, int keycode)
{
    int shifted_table = table & (1 << KG_SHIFT);

    if (table & ~((1 << KG_SHIFT) | (1 << KG_ALTGR)))
        return K_HOLE;

    switch (keycode) {
        case SC_ESC: return K(KT_LATIN, 0x1b);
        case SC_BS: return K(KT_LATIN, 0x7f);
        case SC_TAB: return K(KT_LATIN, '\t');
        case SC_ENTER: return K_ENTER;
        case SC_CTRL: return K_CTRL;
        case SC_LSHIFT: case SC_RSHIFT: return K_SHIFT;
        case SC_ALT: return K_ALT;
        case SC_CAPS: return K_CAPS;
        case SC_NUM: return K_NUM;
        case 0x46: return K_HOLD;
        case SC_KP_MUL: return K_PSTAR;
        case SC_F11: return K(KT_FN, 10);
        case SC_F12: return K(KT_FN, 11);

        /* the keys with an E0 prefix (e0_keycodes) */
        case 96: return K_PENTER;
        case 97: return K_CTRL;
        case 98: return K_PSLASH;
        case 100: return K_ALT;
        case 102: return K_FIND;
        case 103: return K_UP;
        case 104: return K_PGUP;
        case 105: return K_LEFT;
        case 106: return K_RIGHT;
        case 107: return K_SELECT;
        case 108: return K_DOWN;
        case 109: return K_PGDN;
        case 110: return K_INSERT;
        case 111: return K_REMOVE;
        case KEY_PAUSE: return K_PAUSE;
    }

    if (keycode >= SC_F1 && keycode <= SC_F10)
        return K(KT_FN, keycode - SC_F1);

    if (keycode >= SC_KP7 && keycode <= SC_KP_DEL) {
        static const uint16_t keypad[13] = {
            K_P0 + 7, K_P0 + 8, K_P0 + 9, K_PMINUS, K_P0 + 4, K_P0 + 5, K_P0 + 6,
            K_PPLUS, K_P0 + 1, K_P0 + 2, K_P0 + 3, K_P0, K_PDOT,
        };
        return keypad[keycode - SC_KP7];
    }

    if (keycode < 0 || keycode >= 128)
        return K_HOLE;

    char c = shifted_table ? shifted[keycode] : keys[keycode];

    if (!c)
        return K_HOLE;

    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))
        return K(KT_LETTER, c);

    return K(KT_LATIN, (unsigned char) c);
}

/* a sequence was cut by a change of mode */
void vt_keyboard_reset(void)
{
    extended = skip = 0;
}

/* with Alt, ESC before what the key sends */
static void send_key(const char *s)
{
    if (alt)
        send("\033");
    send(s);
}

static void send_char(char c)
{
    char buf[] = {c, '\0'};

    if (!c) {
        /* ^@ */
        if (alt)
            send("\033");
        vt_input(buf, 1);
        return;
    }

    send_key(buf);
}

/* the cursor keys, ESC O x in the application mode (DECCKM) */
static void cursor_key(char dir)
{
    struct vt *vt = vt_foreground();
    char buf[] = {'\033', vt && vt->cursor_keys ? 'O' : '[', dir, '\0'};

    send_key(buf);
}

static void editing_key(const char *s)
{
    if (s[1])
        send_key(s);
    else
        cursor_key(s[0]);
}

/* control characters for Ctrl with a key */
static int control(char c)
{
    if (c >= 'a' && c <= 'z')
        return c - 'a' + 1;

    if (c >= 'A' && c <= 'Z')
        return c - 'A' + 1;

    switch (c) {
        case '@': case '2': case ' ': case '`':
            return 0;
        case '[': case '3': case '{':
            return 0x1b;
        case '\\': case '4': case '|':
            return 0x1c;
        case ']': case '5': case '}':
            return 0x1d;
        case '^': case '6': case '~':
            return 0x1e;
        case '_': case '-': case '/': case '7': case '?':
            return 0x1f;
        case '8':
            return 0x7f;
    }

    return c;
}

/* Shift, Ctrl and Alt, in every mode: they may be held while it changes */
static int modifier(int code, int release, int ext)
{
    switch (code) {
        case SC_LSHIFT:
        case SC_RSHIFT:
            /* E0 2A and E0 36 come with other keys, as if shift was used */
            if (!ext)
                shift = !release;
            return 1;
        case SC_CTRL:
            ctrl = !release;
            return 1;
        case SC_ALT:
            alt = !release;
            return 1;
    }

    return 0;
}

void vt_scancode(int scancode)
{
    /* the modes of a program that is gone are undone first */
    vt_keypress();

    struct vt *vt = vt_foreground();
    int mode = vt ? vt->kb_mode : K_XLATE;

    if (mode == K_RAW)
        send_byte(scancode);

    if (skip) {
        --skip;
        return;
    }

    if (scancode == 0xE0) {
        extended = 1;
        return;
    }

    /* Pause, E1 1D 45 E1 9D C5: pressed and released at once */
    if (scancode == 0xE1) {
        skip = 5;

        if (mode == K_MEDIUMRAW) {
            send_byte(KEY_PAUSE);
            send_byte(KEY_PAUSE | 0x80);
        }
        return;
    }

    int code = scancode & 0x7f, release = scancode & 0x80, ext = extended;
    extended = 0;

    if (mode != K_XLATE && mode != K_UNICODE) {
        modifier(code, release, ext);

        /* the fake shifts aren't keys */
        if (mode != K_MEDIUMRAW || (ext && (code == SC_LSHIFT || code == SC_RSHIFT)))
            return;

        int keycode = ext ? e0_keycodes[code] : code;

        if (keycode)
            send_byte(keycode | release);
        return;
    }

    if (modifier(code, release, ext))
        return;

    switch (code) {
        case SC_CAPS:
            if (!release)
                caps = !caps;
            return;
        case SC_NUM:
            if (!release && !ext)
                num = !num;
            return;
    }

    if (release)
        return;

    /* Ctrl+Alt+Del, also with the keypad's Del */
    if (ctrl && alt && code == SC_KP_DEL) {
        kernel_ctrlaltdel();
        return;
    }

    /* function keys, or switching terminals */
    int fkey = code >= SC_F1 && code <= SC_F10 ? code - SC_F1 :
               code == SC_F11 ? 10 : code == SC_F12 ? 11 : -1;

    if (fkey >= 0) {
        if (alt) {
            vt_switch(fkey);
            return;
        }

        send(fkeys[fkey]);
        return;
    }

    if (ext) {
        switch (code) {
            case SC_ENTER: send_key("\r"); return;
            case 0x35: send_char(ctrl ? control('/') : '/'); return;
            case 0x47: send_key("\033[1~"); return;
            case 0x48: cursor_key('A'); return;
            case 0x49: send_key("\033[5~"); return;
            case 0x4B: cursor_key('D'); return;
            case 0x4D: cursor_key('C'); return;
            case 0x4F: send_key("\033[4~"); return;
            case 0x50: cursor_key('B'); return;
            case 0x51: send_key("\033[6~"); return;
            case 0x52: send_key("\033[2~"); return;
            case 0x53: send_key("\033[3~"); return;
        }

        return;
    }

    switch (code) {
        case SC_ESC: send_char('\033'); return;
        case SC_BS: send_char(ctrl ? '\b' : 0x7f); return;
        case SC_TAB: send_key(shift ? "\033[Z" : "\t"); return;
        case SC_ENTER: send_key("\r"); return;
        case SC_KP_MUL: send_char('*'); return;
    }

    if (code >= SC_KP7 && code <= SC_KP_DEL) {
        int i = code - SC_KP7;

        if ((num && !shift) || !keypad_keys[i])
            send_char(keypad_digits[i]);
        else
            editing_key(keypad_keys[i]);
        return;
    }

    char c = shift ? shifted[code] : keys[code];

    if (!c)
        return;

    if (caps && ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')))
        c ^= 0x20;

    if (ctrl)
        c = control(c);

    send_char(c);
}
