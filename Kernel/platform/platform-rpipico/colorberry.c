/*
 * ColorBerry hardware glue: JDI LPM027M128C panel over SPI and the Beepy
 * keyboard controller over I2C, for the Waveshare RP2040-PiZero.
 *
 * core0 runs the terminal emulator (cb_term.c) from the tty write path.
 * core1 pushes dirty text rows to the panel, toggles EXTCOMIN, and polls the
 * keyboard, so the kernel never waits on the slow 2 MHz panel bus.
 */
#include <kernel.h>
#include <printf.h>
#include "picosdk.h"
#include <hardware/spi.h>
#include <hardware/i2c.h>
#include <hardware/gpio.h>
#include "colorberry.h"
#include "cb_keys.h"

/* ---- Panel ------------------------------------------------------------ */

#define CMD_UPDATE_3BIT 0x80    /* M0=1, M2=0, M3=M4=0: 3-bit data mode */
#define CMD_ALL_CLEAR   0x20    /* M2=1 */

/* [cmd][line][150 bytes of pixels][16 dummy clocks] */
static uint8_t linebuf[2 + CB_LINE_BYTES + 2];

static void lcd_cs(int on)
{
    if (on) {
        gpio_put(CB_LCD_CS_PIN, 1);
        busy_wait_us_32(6);         /* tsSCS >= 6us */
    } else {
        busy_wait_us_32(2);         /* thSCS >= 2us */
        gpio_put(CB_LCD_CS_PIN, 0);
        busy_wait_us_32(6);         /* twSCSL >= 6us */
    }
}

static void lcd_send_line(unsigned y)
{
    linebuf[0] = CMD_UPDATE_3BIT;
    linebuf[1] = (uint8_t)(y + 1);  /* gate lines are numbered from 1 */
    cb_term_render_line(y, linebuf + 2);
    linebuf[2 + CB_LINE_BYTES] = 0;
    linebuf[3 + CB_LINE_BYTES] = 0;
    lcd_cs(1);
    spi_write_blocking(CB_LCD_SPI, linebuf, sizeof(linebuf));
    lcd_cs(0);
}

void cb_lcd_init(void)
{
    static const uint8_t clr[2] = { CMD_ALL_CLEAR, 0 };

    gpio_init(CB_LCD_CS_PIN);
    gpio_set_dir(CB_LCD_CS_PIN, GPIO_OUT);
    gpio_put(CB_LCD_CS_PIN, 0);
    gpio_init(CB_LCD_DISP_PIN);
    gpio_set_dir(CB_LCD_DISP_PIN, GPIO_OUT);
    gpio_put(CB_LCD_DISP_PIN, 0);
    gpio_init(CB_LCD_VCOM_PIN);
    gpio_set_dir(CB_LCD_VCOM_PIN, GPIO_OUT);
    gpio_put(CB_LCD_VCOM_PIN, 0);

    spi_init(CB_LCD_SPI, CB_LCD_SPI_HZ);
    spi_set_format(CB_LCD_SPI, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_set_function(CB_LCD_SCK_PIN, GPIO_FUNC_SPI);
    gpio_set_function(CB_LCD_MOSI_PIN, GPIO_FUNC_SPI);

    /* Power-on sequence (datasheet 5.x): clear pixel memory, then DISP */
    busy_wait_us_32(1000);
    lcd_cs(1);
    spi_write_blocking(CB_LCD_SPI, clr, sizeof(clr));
    lcd_cs(0);
    busy_wait_us_32(1000);          /* T2 >= 1ms */
    gpio_put(CB_LCD_DISP_PIN, 1);
    busy_wait_us_32(100);           /* T3/T4 >= 30us */

    /* Blank text screen; every row starts dirty so core1 paints it */
    cb_term_init();
}

/* ---- tty driver hooks (core0) ----------------------------------------- */

void cb_lcd_putc(uint8_t devn, uint8_t c)
{
    used(devn);
    cb_term_putc(c);
}

ttyready_t cb_lcd_ready(uint8_t devn)
{
    used(devn);
    return TTY_READY_NOW;
}

void cb_lcd_sleeping(uint8_t devn)
{
    used(devn);
}

int cb_kbd_getc(uint8_t devn)
{
    used(devn);
    return cb_kq_get();
}

/* ---- Keyboard controller (core1) -------------------------------------- */

#define REG_CFG         0x02
#define REG_INT         0x03
#define REG_KEY         0x04
#define REG_FIF         0x09
#define REG_CF2         0x14
#define REG_TOX         0x15
#define REG_TOY         0x16
#define REG_BKL         0x05
#define REG_DRIVER_STATE 0x2D
#define REG_WRITE       0x80

#define CFG_REPORT_MODS 0x40
#define CFG_KEY_INT     0x10
#define CFG_OVERFLOW_INT 0x02
#define CFG_OVERFLOW_ON 0x01
#define CF2_TOUCH_INT   0x01
#define INT_TOUCH       0x40

#define I2C_TIMEOUT_US  2000

static uint8_t kbd_ok;
static uint8_t bl_on;
static void button_poll(void);
static uint8_t kbd_touch_state;
static uint32_t kbd_next_poll;
static uint8_t kbd_errors;
static uint32_t kbd_retry;

static int kbd_read(uint8_t reg, uint8_t *buf, size_t len)
{
    if (i2c_write_timeout_us(CB_KBD_I2C, CB_KBD_ADDR, &reg, 1, true, I2C_TIMEOUT_US) != 1)
        return -1;
    if (i2c_read_timeout_us(CB_KBD_I2C, CB_KBD_ADDR, buf, len, false, I2C_TIMEOUT_US) != (int)len)
        return -1;
    return 0;
}

static int kbd_write(uint8_t reg, uint8_t val)
{
    uint8_t b[2] = { (uint8_t)(reg | REG_WRITE), val };
    if (i2c_write_timeout_us(CB_KBD_I2C, CB_KBD_ADDR, b, 2, false, I2C_TIMEOUT_US) != 2)
        return -1;
    return 0;
}

static void kbd_setup(void)
{
    kbd_ok = (kbd_write(REG_CFG, CFG_REPORT_MODS | CFG_KEY_INT |
                                 CFG_OVERFLOW_INT | CFG_OVERFLOW_ON) == 0);
    if (!kbd_ok)
        return;
    kbd_write(REG_CF2, 0);
    kbd_write(REG_DRIVER_STATE, 1);
    kbd_write(REG_BKL, bl_on ? 255 : 0);
    kbd_touch_state = 0;
    kbd_errors = 0;
}

void cb_kbd_core1_init(void)
{
    i2c_init(CB_KBD_I2C, CB_KBD_I2C_HZ);
    gpio_set_function(CB_KBD_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(CB_KBD_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(CB_KBD_SDA_PIN);
    gpio_pull_up(CB_KBD_SCL_PIN);
    cb_keys_reset();
    kbd_setup();
    kbd_next_poll = time_us_32();
}

void cb_kbd_core1_poll(void)
{
    uint8_t v, n, b[2];
    uint32_t now = time_us_32();

    if ((int32_t)(now - kbd_next_poll) < 0)
        return;

    if (!kbd_ok) {
        /* Not answering: keep the side button alive, retry once a second */
        button_poll();
        if ((int32_t)(now - kbd_retry) >= 0) {
            kbd_retry = now + 1000000;
            kbd_setup();
        }
        kbd_next_poll = now + CB_KBD_POLL_US;
        return;
    }
    kbd_next_poll = now + CB_KBD_POLL_US;

    button_poll();

    if (kbd_read(REG_KEY, &n, 1)) {
        if (++kbd_errors > 10)
            kbd_ok = 0;
        return;
    }
    kbd_errors = 0;
    n &= 0x1F;
    while (n--) {
        uint8_t st;
        if (kbd_read(REG_FIF, b, 2))
            break;
        st = (b[1] >> 4) ? (b[1] >> 4) : (b[1] & 0x0F);
        cb_keys_event(b[0], st);
    }

    /* Touchpad: only reported while arrow-key mode is on */
    if (cb_keys_touch_enabled() != kbd_touch_state) {
        kbd_touch_state = cb_keys_touch_enabled();
        kbd_write(REG_CF2, kbd_touch_state ? CF2_TOUCH_INT : 0);
    }
    if (kbd_read(REG_INT, &v, 1) == 0 && v) {
        if ((v & INT_TOUCH) && kbd_touch_state) {
            int8_t dx = 0, dy = 0;
            if (kbd_read(REG_TOX, (uint8_t *)&dx, 1) == 0 &&
                kbd_read(REG_TOY, (uint8_t *)&dy, 1) == 0)
                cb_keys_touch(dx, dy);
        }
        kbd_write(REG_INT, 0);
    }
}

/* ---- Backlight and side button (core1) -------------------------------- */

static uint8_t btn_count;
static uint8_t btn_down;

static void backlight_set(uint8_t on)
{
    bl_on = on;
#ifdef CB_LCD_BL_PIN
    gpio_put(CB_LCD_BL_PIN, on);
#endif
    /* Keyboard backlight follows the screen, as on ColorBerry Linux */
    if (kbd_ok)
        kbd_write(REG_BKL, on ? 255 : 0);
}

static void button_init(void)
{
#ifdef CB_LCD_BL_PIN
    gpio_init(CB_LCD_BL_PIN);
    gpio_set_dir(CB_LCD_BL_PIN, GPIO_OUT);
#endif
    gpio_init(CB_BUTTON_PIN);
    gpio_set_dir(CB_BUTTON_PIN, GPIO_IN);
    gpio_pull_up(CB_BUTTON_PIN);
    backlight_set(0);       /* off at boot: the panel is readable without it */
}

/* Called every keyboard poll: debounce, toggle on release */
static void button_poll(void)
{
    uint8_t pressed = !gpio_get(CB_BUTTON_PIN);

    if (pressed == btn_down) {
        btn_count = 0;
        return;
    }
    if (++btn_count < CB_BUTTON_DEBOUNCE)
        return;
    btn_count = 0;
    btn_down = pressed;
    if (!pressed)
        backlight_set(!bl_on);
}

/* ---- core1 main work -------------------------------------------------- */

static uint32_t vcom_next;
static uint8_t vcom_level;
static uint8_t scan_row;

void cb_core1_init(void)
{
    vcom_next = time_us_32();
    cb_kbd_core1_init();
    button_init();
}

void cb_core1_poll(void)
{
    uint32_t now = time_us_32();
    unsigned i;

    /* EXTCOMIN must keep toggling or the panel slowly degrades */
    if ((int32_t)(now - vcom_next) >= 0) {
        vcom_level ^= 1;
        gpio_put(CB_LCD_VCOM_PIN, vcom_level);
        vcom_next = now + CB_LCD_VCOM_US;
    }

    /* Redraw at most one dirty text row (8 panel lines) per call so USB
       and the keyboard get serviced in between */
    for (i = 0; i < CB_ROWS; i++) {
        unsigned r = scan_row;
        scan_row = (scan_row + 1) % CB_ROWS;
        if (cb_dirty[r]) {
            unsigned y;
            cb_dirty[r] = 0;
            __asm__ volatile("dmb" ::: "memory");
            for (y = r * CB_FONT_H; y < (r + 1) * CB_FONT_H; y++)
                lcd_send_line(y);
            break;
        }
    }

    cb_kbd_core1_poll();
}
