/*
 * Small ANSI/xterm-subset terminal for the ColorBerry LCD.
 *
 * Supports what the "colorberry" termcap entry advertises:
 *   C0:  BEL BS HT LF VT FF CR ESC
 *   ESC: 7 8 D E M c ( ) followed by a charset byte (ignored)
 *   CSI: A B C D E F G H f d J K L M P @ X S T r s u m
 *        ?25h/l (cursor), ?7h/l (autowrap), ?1049h/l (clears), other h/l ignored
 *   SGR: 0 1 4 7 22 24 27 30-37 39 40-47 49 90-97 100-107
 *
 * Text is kept as character + attribute cells. core1 renders whole panel
 * lines straight from the cells, so no pixel framebuffer is needed.
 */
#include "cb_term.h"
#include <string.h>

/* Glyph row r of character c (0x20-0x7F), leftmost pixel in bit GLYPH_W-1 */
#if CB_GLYPH_W == 4
extern const unsigned char font4x8[];
#define GLYPH(c, r)     (font4x8[((c) - 0x20) * 8 + (r)] >> 4)
#elif CB_GLYPH_W == 8
extern const unsigned char fontdata_8x8[];      /* CONFIG_FONT8X8SMALL: 0x20-0x7F */
#define GLYPH(c, r)     (fontdata_8x8[((c) - 0x20) * 8 + (r)])
#else
extern const unsigned char fontdata_6x8[];
#define GLYPH(c, r)     (fontdata_6x8[((c) - 0x20) * 8 + (r)])
#endif

/* Attribute byte: fg in bits 0-2, bg in bits 3-5, underline bit 6 */
#define A_FG(a)     ((a) & 7)
#define A_BG(a)     (((a) >> 3) & 7)
#define A_UL        0x40
#define MKATTR(fg, bg, ul) ((uint8_t)((fg) | ((bg) << 3) | ((ul) ? A_UL : 0)))

static uint8_t cell_ch[CB_ROWS][CB_COLS];
static uint8_t cell_at[CB_ROWS][CB_COLS];
volatile uint8_t cb_dirty[CB_ROWS];

/* Cursor, read by core1 when rendering */
static volatile uint8_t cur_x, cur_y;
static volatile uint8_t cur_on = 1;

/* SGR state */
static uint8_t fg = CB_DEFAULT_FG, bg = CB_DEFAULT_BG, ul, rev;
static uint8_t attr = MKATTR(CB_DEFAULT_FG, CB_DEFAULT_BG, 0);

static uint8_t sav_x, sav_y, sav_fg = CB_DEFAULT_FG, sav_bg = CB_DEFAULT_BG, sav_ul, sav_rev;
static uint8_t top, bot = CB_ROWS - 1;       /* scroll region, inclusive */
static uint8_t wrap_pending;
static uint8_t autowrap = 1;

enum { S_NORMAL, S_ESC, S_CSI, S_SKIP1 };
static uint8_t state;
#define MAXPAR 8
static uint16_t par[MAXPAR];
static uint8_t npar, priv;

/* ANSI colour index (R=1 G=2 B=4) to panel order (R=4 G=2 B=1) */
static uint8_t ansi_to_rgb(uint8_t n)
{
    return (uint8_t)(((n & 1) << 2) | (n & 2) | ((n >> 2) & 1));
}

static void mark(uint8_t r)
{
    cb_dirty[r] = 1;
}

static void mark_range(uint8_t r0, uint8_t r1)
{
    while (r0 <= r1)
        cb_dirty[r0++] = 1;
}

static void update_attr(void)
{
    if (rev)
        attr = MKATTR(bg, fg, ul);
    else
        attr = MKATTR(fg, bg, ul);
}

/* Erased cells take the current background, like xterm (bce) */
static void erase(uint8_t r, uint8_t c0, uint8_t c1)
{
    uint8_t a = rev ? MKATTR(bg, fg, 0) : MKATTR(fg, bg, 0);
    if (c1 >= CB_COLS)
        c1 = CB_COLS - 1;
    if (c0 > c1)
        return;
    memset(&cell_ch[r][c0], ' ', c1 - c0 + 1);
    memset(&cell_at[r][c0], a, c1 - c0 + 1);
    mark(r);
}

static void scroll_up(uint8_t t, uint8_t b, uint8_t n)
{
    if (n > b - t + 1)
        n = b - t + 1;
    if (n < b - t + 1) {
        memmove(cell_ch[t], cell_ch[t + n], (b - t + 1 - n) * CB_COLS);
        memmove(cell_at[t], cell_at[t + n], (b - t + 1 - n) * CB_COLS);
    }
    for (uint8_t r = b - n + 1; r <= b; r++)
        erase(r, 0, CB_COLS - 1);
    mark_range(t, b);
}

static void scroll_down(uint8_t t, uint8_t b, uint8_t n)
{
    if (n > b - t + 1)
        n = b - t + 1;
    if (n < b - t + 1) {
        memmove(cell_ch[t + n], cell_ch[t], (b - t + 1 - n) * CB_COLS);
        memmove(cell_at[t + n], cell_at[t], (b - t + 1 - n) * CB_COLS);
    }
    for (uint8_t r = t; r < t + n; r++)
        erase(r, 0, CB_COLS - 1);
    mark_range(t, b);
}

static void linefeed(void)
{
    if (cur_y == bot)
        scroll_up(top, bot, 1);
    else if (cur_y < CB_ROWS - 1)
        cur_y++;
}

static void reverse_linefeed(void)
{
    if (cur_y == top)
        scroll_down(top, bot, 1);
    else if (cur_y > 0)
        cur_y--;
}

static void put_glyph(uint8_t c)
{
    if (wrap_pending) {
        wrap_pending = 0;
        cur_x = 0;
        linefeed();
    }
    cell_ch[cur_y][cur_x] = c;
    cell_at[cur_y][cur_x] = attr;
    mark(cur_y);
    if (cur_x == CB_COLS - 1) {
        if (autowrap)
            wrap_pending = 1;
    } else
        cur_x++;
}

static void reset_sgr(void)
{
    fg = CB_DEFAULT_FG;
    bg = CB_DEFAULT_BG;
    ul = rev = 0;
    update_attr();
}

static void sgr(void)
{
    uint8_t i;
    if (npar == 0) {
        reset_sgr();
        return;
    }
    for (i = 0; i < npar; i++) {
        uint16_t p = par[i];
        if (p == 0)
            reset_sgr();
        else if (p == 4)
            ul = 1;
        else if (p == 7)
            rev = 1;
        else if (p == 24)
            ul = 0;
        else if (p == 27)
            rev = 0;
        else if (p >= 30 && p <= 37)
            fg = ansi_to_rgb(p - 30);
        else if (p == 39)
            fg = CB_DEFAULT_FG;
        else if (p >= 40 && p <= 47)
            bg = ansi_to_rgb(p - 40);
        else if (p == 49)
            bg = CB_DEFAULT_BG;
        else if (p >= 90 && p <= 97)
            fg = ansi_to_rgb(p - 90);
        else if (p >= 100 && p <= 107)
            bg = ansi_to_rgb(p - 100);
        /* 1 (bold), 22 and everything else: no visible change */
    }
    update_attr();
}

static void clamp_cursor(void)
{
    if (cur_x >= CB_COLS)
        cur_x = CB_COLS - 1;
    if (cur_y >= CB_ROWS)
        cur_y = CB_ROWS - 1;
}

/* Parameter n (1 based count) with a default */
static uint16_t P(uint8_t n, uint16_t def)
{
    if (n >= npar || par[n] == 0)
        return def;
    return par[n];
}

static void csi_dispatch(uint8_t c)
{
    uint16_t n = P(0, 1);
    uint8_t r;

    wrap_pending = 0;
    /* Keep counts within the screen so 8-bit maths below cannot wrap */
    if (n > 255)
        n = 255;

    if (priv) {
        if (c == 'h' || c == 'l') {
            uint8_t on = (c == 'h');
            for (r = 0; r < npar; r++) {
                if (par[r] == 25) {
                    cur_on = on;
                    mark(cur_y);
                } else if (par[r] == 7)
                    autowrap = on;
                else if (par[r] == 1049 || par[r] == 47 || par[r] == 1047) {
                    /* No second screen: just start with a clean one */
                    for (uint8_t y = 0; y < CB_ROWS; y++)
                        erase(y, 0, CB_COLS - 1);
                    cur_x = cur_y = 0;
                }
            }
        }
        return;
    }

    switch (c) {
    case 'A':
        cur_y = (cur_y >= top && cur_y - n < top) ? top : (cur_y > n ? cur_y - n : 0);
        break;
    case 'B':
    case 'e':
        if (cur_y <= bot && cur_y + n > bot)
            cur_y = bot;
        else
            cur_y = (cur_y + n >= CB_ROWS) ? CB_ROWS - 1 : cur_y + n;
        break;
    case 'C':
    case 'a':
        cur_x = (cur_x + n >= CB_COLS) ? CB_COLS - 1 : cur_x + n;
        break;
    case 'D':
        cur_x = (cur_x > n) ? cur_x - n : 0;
        break;
    case 'E':
        cur_x = 0;
        cur_y = (cur_y + n >= CB_ROWS) ? CB_ROWS - 1 : cur_y + n;
        break;
    case 'F':
        cur_x = 0;
        cur_y = (cur_y > n) ? cur_y - n : 0;
        break;
    case 'G':
    case '`':
        cur_x = n - 1;
        break;
    case 'd':
        cur_y = n - 1;
        break;
    case 'H':
    case 'f': {
        uint16_t y = P(0, 1), x = P(1, 1);
        cur_y = (y > CB_ROWS) ? CB_ROWS - 1 : y - 1;
        cur_x = (x > CB_COLS) ? CB_COLS - 1 : x - 1;
        break;
    }
    case 'J':
        switch (P(0, 0)) {
        case 0:
            erase(cur_y, cur_x, CB_COLS - 1);
            for (r = cur_y + 1; r < CB_ROWS; r++)
                erase(r, 0, CB_COLS - 1);
            break;
        case 1:
            for (r = 0; r < cur_y; r++)
                erase(r, 0, CB_COLS - 1);
            erase(cur_y, 0, cur_x);
            break;
        default:
            for (r = 0; r < CB_ROWS; r++)
                erase(r, 0, CB_COLS - 1);
            break;
        }
        break;
    case 'K':
        switch (P(0, 0)) {
        case 0:
            erase(cur_y, cur_x, CB_COLS - 1);
            break;
        case 1:
            erase(cur_y, 0, cur_x);
            break;
        default:
            erase(cur_y, 0, CB_COLS - 1);
            break;
        }
        break;
    case 'L':
        if (cur_y >= top && cur_y <= bot)
            scroll_down(cur_y, bot, n);
        break;
    case 'M':
        if (cur_y >= top && cur_y <= bot)
            scroll_up(cur_y, bot, n);
        break;
    case 'P':
        if (n > CB_COLS - cur_x)
            n = CB_COLS - cur_x;
        memmove(&cell_ch[cur_y][cur_x], &cell_ch[cur_y][cur_x + n], CB_COLS - cur_x - n);
        memmove(&cell_at[cur_y][cur_x], &cell_at[cur_y][cur_x + n], CB_COLS - cur_x - n);
        erase(cur_y, CB_COLS - n, CB_COLS - 1);
        break;
    case '@':
        if (n > CB_COLS - cur_x)
            n = CB_COLS - cur_x;
        memmove(&cell_ch[cur_y][cur_x + n], &cell_ch[cur_y][cur_x], CB_COLS - cur_x - n);
        memmove(&cell_at[cur_y][cur_x + n], &cell_at[cur_y][cur_x], CB_COLS - cur_x - n);
        erase(cur_y, cur_x, cur_x + n - 1);
        break;
    case 'X':
        if (n > CB_COLS - cur_x)
            n = CB_COLS - cur_x;
        erase(cur_y, cur_x, cur_x + n - 1);
        break;
    case 'S':
        scroll_up(top, bot, n);
        break;
    case 'T':
        scroll_down(top, bot, n);
        break;
    case 'r': {
        uint16_t t = P(0, 1), b = P(1, CB_ROWS);
        if (b > CB_ROWS)
            b = CB_ROWS;
        if (t < b) {
            top = t - 1;
            bot = b - 1;
            cur_x = 0;
            cur_y = 0;
        }
        break;
    }
    case 's':
        sav_x = cur_x;
        sav_y = cur_y;
        break;
    case 'u':
        cur_x = sav_x;
        cur_y = sav_y;
        break;
    case 'm':
        sgr();
        break;
    default:
        break;
    }
    clamp_cursor();
}

static void term_reset(void)
{
    uint8_t r;
    top = 0;
    bot = CB_ROWS - 1;
    autowrap = 1;
    wrap_pending = 0;
    cur_on = 1;
    reset_sgr();
    for (r = 0; r < CB_ROWS; r++)
        erase(r, 0, CB_COLS - 1);
    cur_x = cur_y = 0;
    state = S_NORMAL;
}

static void term_putc(uint8_t c)
{
    switch (state) {
    case S_ESC:
        state = S_NORMAL;
        switch (c) {
        case '[':
            state = S_CSI;
            npar = 0;
            priv = 0;
            memset(par, 0, sizeof(par));
            return;
        case '7':
            sav_x = cur_x; sav_y = cur_y;
            sav_fg = fg; sav_bg = bg; sav_ul = ul; sav_rev = rev;
            return;
        case '8':
            cur_x = sav_x; cur_y = sav_y;
            fg = sav_fg; bg = sav_bg; ul = sav_ul; rev = sav_rev;
            update_attr();
            wrap_pending = 0;
            return;
        case 'D':
            linefeed();
            return;
        case 'E':
            cur_x = 0;
            linefeed();
            return;
        case 'M':
            reverse_linefeed();
            return;
        case 'c':
            term_reset();
            return;
        case '(':
        case ')':
            state = S_SKIP1;
            return;
        default:
            return;     /* ignore unknown ESC x (e.g. ESC = / ESC >) */
        }
    case S_SKIP1:
        state = S_NORMAL;
        return;
    case S_CSI:
        if (c >= '0' && c <= '9') {
            if (npar == 0)
                npar = 1;
            if (npar <= MAXPAR) {
                uint16_t v = par[npar - 1] * 10 + (c - '0');
                par[npar - 1] = v > 9999 ? 9999 : v;
            }
            return;
        }
        if (c == ';') {
            if (npar == 0)
                npar = 1;
            if (npar < MAXPAR)
                npar++;
            return;
        }
        if (c == '?' || c == '>' || c == '!') {
            priv = 1;
            return;
        }
        if (c >= 0x40 && c <= 0x7E) {
            state = S_NORMAL;
            csi_dispatch(c);
            return;
        }
        if (c == 0x1B) {
            state = S_ESC;
            return;
        }
        /* intermediate bytes: ignore */
        return;
    default:
        break;
    }

    switch (c) {
    case 0x07:      /* BEL */
        return;
    case 0x08:      /* BS */
        wrap_pending = 0;
        if (cur_x)
            cur_x--;
        return;
    case 0x09:      /* HT */
        wrap_pending = 0;
        cur_x = (cur_x | 7) + 1;
        if (cur_x >= CB_COLS)
            cur_x = CB_COLS - 1;
        return;
    case 0x0A:      /* LF, VT, FF */
    case 0x0B:
    case 0x0C:
        wrap_pending = 0;
        linefeed();
        return;
    case 0x0D:      /* CR */
        wrap_pending = 0;
        cur_x = 0;
        return;
    case 0x1B:
        state = S_ESC;
        return;
    default:
        if (c < 0x20 || c == 0x7F)
            return;
        put_glyph(c);
    }
}

void cb_term_putc(uint8_t c)
{
    uint8_t oy = cur_y;
    term_putc(c);
    /* The cursor is drawn by the renderer, so both rows need redrawing
       when it moves between rows */
    if (oy != cur_y) {
        mark(oy);
        mark(cur_y);
    } else if (cur_on)
        mark(cur_y);
}

void cb_term_init(void)
{
    sav_x = sav_y = 0;
    term_reset();
}

/*
 * Render panel line y (0..239). Each pixel is three bits R,G,B sent MSB
 * first, pixel 0 at the left. A '1' bit lights that sub-pixel.
 */
void cb_term_render_line(unsigned y, uint8_t *out)
{
    unsigned row = y / CB_FONT_H;
    unsigned gy = (y % CB_FONT_H) / CB_SCALE_Y;
    const uint8_t *ch = cell_ch[row];
    const uint8_t *at = cell_at[row];
    int curcol = (cur_on && cur_y == row) ? cur_x : -1;
    uint32_t acc = 0;
    unsigned nbits = 0, o = 0, c;
    int i, s;

    for (c = 0; c < CB_COLS; c++) {
        uint8_t chr = ch[c];
        uint8_t a = at[c];
        uint8_t f = A_FG(a), b = A_BG(a);
        uint8_t g;

        if (chr >= 0x20 && chr < 0x80)
            g = GLYPH(chr, gy);
        else
            g = 0;
        if ((a & A_UL) && gy == CB_GLYPH_H - 1)
            g = (1 << CB_GLYPH_W) - 1;
        if ((int)c == curcol) {
            uint8_t t = f;
            f = b;
            b = t;
        }
        for (i = CB_GLYPH_W - 1; i >= 0; i--) {
            uint8_t px = ((g >> i) & 1) ? f : b;
            for (s = 0; s < CB_SCALE_X; s++) {
                acc = (acc << 3) | px;
                nbits += 3;
                if (nbits >= 8) {
                    nbits -= 8;
                    out[o++] = (uint8_t)(acc >> nbits);
                }
            }
        }
    }
    /* Right margin (e.g. 400 - 66*6 = 4 pixels) in the default background */
    for (c = CB_COLS * CB_FONT_W; c < CB_LCD_WIDTH; c++) {
        acc = (acc << 3) | CB_DEFAULT_BG;
        nbits += 3;
        if (nbits >= 8) {
            nbits -= 8;
            out[o++] = (uint8_t)(acc >> nbits);
        }
    }
}
