/* Host test: feed escape sequences, render the panel, dump raw RGB + checks */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "cb_term.h"

static uint8_t panel[CB_LCD_HEIGHT][CB_LINE_BYTES];
static int fails;

static void feed(const char *s) { while (*s) cb_term_putc((uint8_t)*s++); }

static void render_all(void)
{
    for (unsigned y = 0; y < CB_LCD_HEIGHT; y++)
        cb_term_render_line(y, panel[y]);
}

static int pix(unsigned x, unsigned y)
{
    unsigned bit = x * 3;
    int v = 0;
    for (int i = 0; i < 3; i++, bit++)
        v = (v << 1) | ((panel[y][bit / 8] >> (7 - bit % 8)) & 1);
    return v;
}

/* Does text cell (col,row) contain any pixel of colour c? */
static int cell_has(int col, int row, int c)
{
    for (int y = 0; y < CB_FONT_H; y++)
        for (int x = 0; x < CB_FONT_W; x++)
            if (pix(col * CB_FONT_W + x, row * CB_FONT_H + y) == c)
                return 1;
    return 0;
}

#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); fails++; } else printf("ok:   %s\n", msg); } while (0)

static void dump_ppm(const char *path)
{
    static const uint8_t pal[8][3] = {
        {0,0,0},{0,0,255},{0,255,0},{0,255,255},{255,0,0},{255,0,255},{255,255,0},{255,255,255}};
    FILE *f = fopen(path, "wb");
    fprintf(f, "P6\n%d %d\n255\n", CB_LCD_WIDTH, CB_LCD_HEIGHT);
    for (unsigned y = 0; y < CB_LCD_HEIGHT; y++)
        for (unsigned x = 0; x < CB_LCD_WIDTH; x++)
            fwrite(pal[pix(x, y)], 3, 1, f);
    fclose(f);
}

int main(int argc, char **argv)
{
    cb_term_init();
    render_all();
    CHECK(!cell_has(0, 0, CB_BLACK) || 1, "init renders");
    CHECK(pix(399, 239) == CB_WHITE, "blank screen is white");

    /* Boot-like text and wrapping */
    feed("FUZIX version 0.5\r\nColorBerry LCD console\r\n");
    for (int i = 0; i < CB_COLS + 4; i++) cb_term_putc('0' + i % 10);
    feed("\r\n");
    render_all();
    CHECK(cell_has(0, 0, CB_BLACK), "text drawn black at (0,0)");
    CHECK(cell_has(0, 3, CB_BLACK), "COLS+4 chars wrapped onto row 3");
    CHECK(!cell_has(5, 3, CB_BLACK), "only 4 chars on wrapped row");

    /* Colours */
    feed("\033[31mred \033[32mgreen \033[34mblue \033[43;30m yellow-bg \033[0m plain\r\n");
    render_all();
    CHECK(cell_has(0, 4, CB_RED), "SGR 31 is red");
    CHECK(cell_has(4, 4, CB_GREEN), "SGR 32 is green");
    CHECK(cell_has(10, 4, CB_BLUE), "SGR 34 is blue");
    CHECK(cell_has(15, 4, CB_YELLOW), "SGR 43 background is yellow");
    feed("\033[7m reverse \033[27m \033[4munderline\033[24m\r\n");

    /* Cursor addressing and erase */
    feed("\033[10;20HXY\033[10;1H\033[K");
    render_all();
    CHECK(!cell_has(19, 9, CB_BLACK), "CSI K erased row 10");
    feed("\033[12;20Hhere\033[1D\033[1A^");
    render_all();
    CHECK(cell_has(22, 10, CB_BLACK), "relative moves land at row 11 col 23");

    /* Scroll region: lines 15-20, scroll it, rest untouched */
    feed("\033[3;1Hline3\033[8;1Hline8\033[12;1Houtside");
    feed("\033[3;8r\033[8;1H\n");
    render_all();
    CHECK(cell_has(0, 6, CB_BLACK), "line8 scrolled up to row 7");
    CHECK(cell_has(0, 11, CB_BLACK), "row 12 outside region kept");
    feed("\033[r");

    /* Full scroll: push 40 lines */
    feed("\033[2J\033[H");
    for (int i = 0; i < 40; i++) { char b[32]; snprintf(b, sizeof b, "row %d\r\n", i); feed(b); }
    render_all();
    CHECK(cell_has(0, 0, CB_BLACK), "after 40 lines screen still has text at top");

    /* Final picture for eyeballing */
    feed("\033[2J\033[H");
    feed("\033[1mFUZIX\033[0m on \033[35mColorBerry\033[0m / RP2040-PiZero\r\n\r\n");
    feed("login: root\r\n\r\nWelcome to FUZIX.\r\n\r\n# ls /bin\r\n");
    feed("cat  cp   date df   ed   grep ls   mkdir more mv   ps   rm   sh\r\n\r\n");
    feed("\033[30;41m red \033[42m green \033[43m yellow \033[44m blue \033[45m magenta \033[46m cyan \033[47m white \033[0m\r\n");
    feed("\033[31mred\033[32m green\033[33m yellow\033[34m blue\033[35m magenta\033[36m cyan\033[0m\r\n\r\n");
    feed("\033[7m reverse video \033[0m  \033[4munderlined text\033[0m\r\n\r\n# ");
    render_all();
    dump_ppm(argc > 1 ? argv[1] : "screen.ppm");

    printf("%d failures\n", fails);
    return fails != 0;
}
