/*
 * Beepy/ColorBerry keyboard translation.
 *
 * The keyboard controller (beepy-fw) reports USB HID scancodes plus a key
 * state. This mirrors the default layout of the Linux beepy-kbd driver:
 *
 *   Shift          sticky / held, upper case and shifted punctuation
 *   Alt (left)     the printed symbol layer of the Q20 keyboard
 *   Sym            extra symbols  ~ ` { } [ ] < > ^ % = \ | & "
 *   Call           Control (sticky / held)
 *   Back           Escape (or leaves touchpad mode)
 *   Berry          Tab
 *   Touchpad click turns on arrow-key mode, then sends Enter
 *   Alt+Enter      Tab        Alt+Backspace  Delete (0x7F)
 *   Alt+$          ~
 *
 * Sticky modifiers: tap a modifier and it applies to the next key; hold it
 * and it applies while held.
 */
#include "cb_keys.h"

/* HID scancodes used by beepy-fw */
#define HID_A           4
#define HID_Z           29
#define HID_1           30
#define HID_0           39
#define HID_ENTER       40
#define HID_ESC         41      /* Back */
#define HID_BACKSPACE   42
#define HID_TAB         43
#define HID_SPACE       44
#define HID_MINUS       45
#define HID_SLASH       56
#define HID_RIGHT       79
#define HID_LEFT        80
#define HID_DOWN        81
#define HID_UP          82
#define HID_COMPOSE     101     /* touchpad click */
#define HID_OPEN        116     /* Call: Control */
#define HID_PROPS       118     /* Berry */
#define HID_STOP        120     /* Power */
#define HID_MUTE        127     /* the "$" key */
#define HID_LCTRL       224
#define HID_LSHIFT      225
#define HID_LALT        226     /* physical Alt */
#define HID_RSHIFT      229
#define HID_RALT        230     /* Sym */

/* ---- core1 -> core0 byte queue (single producer, single consumer) ---- */

#define KQ_SIZE 64
static volatile uint8_t kq[KQ_SIZE];
static volatile uint8_t kq_head, kq_tail;

static inline void barrier(void)
{
    __asm__ volatile("" ::: "memory");
#if defined(__arm__)
    __asm__ volatile("dmb" ::: "memory");
#endif
}

void cb_kq_put(uint8_t c)
{
    uint8_t n = (kq_head + 1) & (KQ_SIZE - 1);
    if (n == kq_tail)
        return;                 /* full: drop */
    kq[kq_head] = c;
    barrier();
    kq_head = n;
}

int cb_kq_get(void)
{
    uint8_t c;
    if (kq_tail == kq_head)
        return -1;
    barrier();
    c = kq[kq_tail];
    barrier();
    kq_tail = (kq_tail + 1) & (KQ_SIZE - 1);
    return c;
}

static void put_str(const char *s)
{
    while (*s)
        cb_kq_put((uint8_t)*s++);
}

/* ---- Layout tables ---------------------------------------------------- */

/* Alt layer, indexed by letter (printed on the Q20 keys) */
static const char alt_letter[26] = {
    '*', '!', '9', '5', '2', '6', '/', ':', '-', ';', '\'', '"', '.',
    ',', '+', '@', '#', '3', '4', '(', '_', '?', '1', '8', ')', '7'
};

/* Sym layer, indexed by letter; 0 = nothing in ASCII */
static const char sym_letter[26] = {
    '=', 0, 0, 0, '{', 0, '\\', '|', '>', '&', '"', '"', 0,
    0, '^', '%', '~', '}', 0, '[', '<', 0, '`', 0, ']', 0
};

/* HID 45..56, unshifted and shifted (US layout) */
static const char punct[12] = { '-', '=', '[', ']', '\\', 0, ';', '\'', '`', ',', '.', '/' };
static const char punct_s[12] = { '_', '+', '{', '}', '|', 0, ':', '"', '~', '<', '>', '?' };
static const char digit_s[10] = { '!', '@', '#', '$', '%', '^', '&', '*', '(', ')' };

/* ---- Sticky modifiers ------------------------------------------------- */

struct mod {
    uint8_t held;
    uint8_t sticky;
    uint8_t used;
};

static struct mod m_shift, m_ctrl, m_alt, m_sym;
static uint8_t touch_on;
static int16_t touch_x, touch_y;
#define TOUCH_THRESHOLD 8

static void mod_event(struct mod *m, uint8_t state)
{
    if (state == CB_KS_PRESSED) {
        m->held = 1;
        m->used = 0;
    } else if (state == CB_KS_RELEASED) {
        m->held = 0;
        if (!m->used)
            m->sticky = !m->sticky;
    }
}

static uint8_t mod_active(struct mod *m)
{
    return m->held || m->sticky;
}

/* Called once a non-modifier key has been delivered */
static void mods_consume(void)
{
    struct mod *all[4] = { &m_shift, &m_ctrl, &m_alt, &m_sym };
    for (int i = 0; i < 4; i++) {
        if (all[i]->held)
            all[i]->used = 1;
        all[i]->sticky = 0;
    }
}

void cb_keys_reset(void)
{
    m_shift = m_ctrl = m_alt = m_sym = (struct mod){ 0, 0, 0 };
    touch_on = 0;
    touch_x = touch_y = 0;
    kq_head = kq_tail = 0;
}

uint8_t cb_keys_touch_enabled(void)
{
    return touch_on;
}

static void emit(char c)
{
    if (!c)
        return;
    if (mod_active(&m_ctrl)) {
        if (c >= '@' && c <= '~')
            c &= 0x1F;
        else if (c == ' ')
            c = 0;
        else if (c == '?')
            c = 0x7F;
    }
    cb_kq_put((uint8_t)c);
}

void cb_keys_event(uint8_t sc, uint8_t state)
{
    char c = 0;

    switch (sc) {
    case HID_LSHIFT:
    case HID_RSHIFT:
        mod_event(&m_shift, state);
        return;
    case HID_OPEN:
    case HID_LCTRL:
        mod_event(&m_ctrl, state);
        return;
    case HID_LALT:
        mod_event(&m_alt, state);
        return;
    case HID_RALT:
        mod_event(&m_sym, state);
        return;
    }

    /* Everything else acts on the initial press only */
    if (state != CB_KS_PRESSED)
        return;

    if (sc >= HID_A && sc <= HID_Z) {
        uint8_t l = sc - HID_A;
        if (mod_active(&m_sym))
            c = sym_letter[l];
        else if (mod_active(&m_alt))
            c = alt_letter[l];
        else if (mod_active(&m_shift))
            c = 'A' + l;
        else
            c = 'a' + l;
        emit(c);
        mods_consume();
        return;
    }

    if (sc >= HID_1 && sc <= HID_0) {
        uint8_t d = sc - HID_1;
        c = mod_active(&m_shift) ? digit_s[d] : (d == 9 ? '0' : '1' + d);
        emit(c);
        mods_consume();
        return;
    }

    if (sc >= HID_MINUS && sc <= HID_SLASH) {
        c = mod_active(&m_shift) ? punct_s[sc - HID_MINUS] : punct[sc - HID_MINUS];
        emit(c);
        mods_consume();
        return;
    }

    switch (sc) {
    case HID_ENTER:
        c = mod_active(&m_alt) ? '\t' : '\r';
        break;
    case HID_BACKSPACE:
        c = mod_active(&m_alt) ? 0x7F : 0x08;
        break;
    case HID_TAB:
    case HID_PROPS:
        c = '\t';
        break;
    case HID_SPACE:
        c = ' ';
        break;
    case HID_MUTE:
        c = mod_active(&m_alt) ? '~' : '$';
        break;
    case HID_ESC:
        if (touch_on) {
            touch_on = 0;
            mods_consume();
            return;
        }
        cb_kq_put(0x1B);
        mods_consume();
        return;
    case HID_COMPOSE:
        if (!touch_on) {
            touch_on = 1;
            touch_x = touch_y = 0;
            return;
        }
        c = '\r';
        break;
    case HID_UP:
        put_str("\033[A");
        mods_consume();
        return;
    case HID_DOWN:
        put_str("\033[B");
        mods_consume();
        return;
    case HID_RIGHT:
        put_str("\033[C");
        mods_consume();
        return;
    case HID_LEFT:
        put_str("\033[D");
        mods_consume();
        return;
    case HID_STOP:
    default:
        return;
    }
    emit(c);
    mods_consume();
}

void cb_keys_touch(int8_t dx, int8_t dy)
{
    if (!touch_on)
        return;

    touch_x += dx;
    touch_y += dy;
    /* Drop small drift on an axis that is not moving */
    if (dx == 0 && touch_x > -TOUCH_THRESHOLD && touch_x < TOUCH_THRESHOLD)
        touch_x = 0;
    if (dy == 0 && touch_y > -TOUCH_THRESHOLD && touch_y < TOUCH_THRESHOLD)
        touch_y = 0;

    while (touch_x <= -TOUCH_THRESHOLD) {
        put_str("\033[D");
        touch_x += TOUCH_THRESHOLD;
    }
    while (touch_x >= TOUCH_THRESHOLD) {
        put_str("\033[C");
        touch_x -= TOUCH_THRESHOLD;
    }
    while (touch_y <= -TOUCH_THRESHOLD) {
        put_str("\033[A");
        touch_y += TOUCH_THRESHOLD;
    }
    while (touch_y >= TOUCH_THRESHOLD) {
        put_str("\033[B");
        touch_y -= TOUCH_THRESHOLD;
    }
}
