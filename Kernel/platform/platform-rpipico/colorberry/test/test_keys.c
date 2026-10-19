#include <stdio.h>
#include <string.h>
#include "cb_keys.h"

static int fails;
static void press(uint8_t sc) { cb_keys_event(sc, CB_KS_PRESSED); cb_keys_event(sc, CB_KS_RELEASED); }
static void down(uint8_t sc) { cb_keys_event(sc, CB_KS_PRESSED); }
static void up(uint8_t sc) { cb_keys_event(sc, CB_KS_RELEASED); }

static void expect(const char *want, const char *what)
{
    char got[64]; int n = 0, c;
    while ((c = cb_kq_get()) >= 0 && n < 63) got[n++] = (char)c;
    got[n] = 0;
    if (strcmp(got, want)) { fails++; printf("FAIL: %s: got [", what); for (int i = 0; i < n; i++) printf("%02x ", (uint8_t)got[i]); printf("]\n"); }
    else printf("ok:   %s\n", what);
}

#define A(l) (4 + ((l) - 'a'))

int main(void)
{
    cb_keys_reset();
    press(A('h')); press(A('i')); expect("hi", "plain letters");
    press(225); press(A('h')); press(A('i')); expect("Hi", "sticky shift applies once");
    down(225); press(A('a')); press(A('b')); up(225); press(A('c')); expect("ABc", "held shift, then released");
    press(226); press(A('w')); expect("1", "alt+w = 1");
    press(226); press(A('q')); expect("#", "alt+q = #");
    press(230); press(A('t')); expect("[", "sym+t = [");
    press(116); press(A('c')); expect("\x03", "call+c = ^C");
    down(116); press(A('d')); up(116); expect("\x04", "held call+d = ^D");
    press(40); expect("\r", "enter");
    press(42); expect("\x08", "backspace");
    press(226); press(42); expect("\x7f", "alt+backspace = DEL");
    press(127); expect("$", "$ key");
    press(41); expect("\x1b", "back = ESC");
    press(118); expect("\t", "berry = tab");
    press(101); expect("", "touch click enables touch");
    cb_keys_touch(-20, 0); expect("\033[D\033[D", "swipe left gives 2 arrows");
    cb_keys_touch(0, 9); expect("\033[B", "swipe down");
    press(101); expect("\r", "click in touch mode = enter");
    press(41); expect("", "back leaves touch mode");
    cb_keys_touch(30, 0); expect("", "touch ignored when off");
    cb_keys_event(A('x'), CB_KS_HOLD); expect("", "hold state ignored for letters");
    printf("%d failures\n", fails);
    return fails != 0;
}
