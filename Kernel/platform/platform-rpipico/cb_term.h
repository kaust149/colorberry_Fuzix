/*
 * Small ANSI/xterm-subset terminal for the ColorBerry LCD.
 *
 * Hardware independent so it can be unit tested on a host. core0 feeds
 * characters in with cb_term_putc(); core1 turns dirty rows into panel
 * line data with cb_term_render_line().
 */
#ifndef CB_TERM_H
#define CB_TERM_H

#include <stdint.h>

#define CB_LCD_WIDTH        400
#define CB_LCD_HEIGHT       240
#define CB_FONT_W           6
#define CB_FONT_H           8
#define CB_COLS             (CB_LCD_WIDTH / CB_FONT_W)     /* 66 */
#define CB_ROWS             (CB_LCD_HEIGHT / CB_FONT_H)    /* 30 */
#define CB_LINE_BYTES       (CB_LCD_WIDTH * 3 / 8)         /* 150 */

/* 3-bit colours in panel order: bit2=R bit1=G bit0=B */
#define CB_BLACK    0
#define CB_BLUE     1
#define CB_GREEN    2
#define CB_CYAN     3
#define CB_RED      4
#define CB_MAGENTA  5
#define CB_YELLOW   6
#define CB_WHITE    7

/* Paper-white by default (the panel is reflective). */
#ifndef CB_DEFAULT_FG
#define CB_DEFAULT_FG   CB_BLACK
#endif
#ifndef CB_DEFAULT_BG
#define CB_DEFAULT_BG   CB_WHITE
#endif

/* Set by core0 when a text row changes, cleared by core1 before drawing. */
extern volatile uint8_t cb_dirty[CB_ROWS];

extern void cb_term_init(void);
extern void cb_term_putc(uint8_t c);
/* Fill 'out' with CB_LINE_BYTES of 3-bit pixel data for panel line y. */
extern void cb_term_render_line(unsigned y, uint8_t *out);

#endif
