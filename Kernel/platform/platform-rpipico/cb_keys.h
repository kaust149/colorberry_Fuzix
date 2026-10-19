/*
 * Beepy/ColorBerry keyboard translation: firmware key events in, terminal
 * bytes out. Hardware independent (unit tested on a host).
 */
#ifndef CB_KEYS_H
#define CB_KEYS_H

#include <stdint.h>

/* Key states reported by the keyboard firmware */
#define CB_KS_PRESSED   1
#define CB_KS_HOLD      2
#define CB_KS_RELEASED  3
#define CB_KS_LONGHOLD  4

/* Byte queue from core1 (producer) to core0 (consumer) */
extern void cb_kq_put(uint8_t c);
extern int cb_kq_get(void);

extern void cb_keys_reset(void);
/* One FIFO entry from the keyboard controller */
extern void cb_keys_event(uint8_t scancode, uint8_t state);
/* Touchpad motion since the last read */
extern void cb_keys_touch(int8_t dx, int8_t dy);
/* Non-zero while the touchpad should be producing arrow keys */
extern uint8_t cb_keys_touch_enabled(void);

#endif
