/*
 * ColorBerry handheld support for the FUZIX RP2040 port.
 *
 * Target: Waveshare RP2040-PiZero fitted in a ColorBerry (Beepy-family)
 * carrier in place of the usual Raspberry Pi Zero 2 W.
 *
 *   Display : JDI LPM027M128C 400x240 3-bit colour memory-in-pixel LCD (SPI)
 *   Keyboard: Beepy-style RP2040 keyboard controller (I2C, addr 0x1F)
 *
 * Pin mapping. The carrier is wired for the Pi Zero header (BCM numbers).
 * The RP2040-PiZero does NOT route BCM n to RP2040 GPIO n for every pin,
 * so every pin used here is collected below. Please check them against the
 * silkscreen on the back of your board before first power-up.
 *
 *   Function        Pi header pin  Pi BCM   RP2040-PiZero GPIO
 *   LCD SCLK        23             11       GP10  (SPI1 SCK)
 *   LCD SI (MOSI)   19             10       GP11  (SPI1 TX)
 *   LCD SCS (CE0)   24             8        GP8   (GPIO, active high)
 *   LCD DISP        15             22       GP22
 *   LCD EXTCOMIN    16             23       GP23
 *   LCD backlight   12             18       GP18  -- shared with on-board
 *                                                    SD SCK, see below
 *   Side button     11             17       GP17
 *   KBD SDA         3              2        GP2   (I2C1 SDA)
 *   KBD SCL         5              3        GP3   (I2C1 SCL)
 *   KBD INT         7              4        GP14  (unused, we poll)
 *
 * On the RP2040-PiZero the on-board TF slot uses GP18..GP21 (SPI0), which
 * also appear on header pins 12/35/38/40. That means the ColorBerry
 * backlight line follows the SD clock; the backlight is therefore left
 * under SD control and not driven by this code.
 */
#ifndef COLORBERRY_H
#define COLORBERRY_H

#include <stdint.h>
#include <tty.h>
#include "cb_term.h"

/* ---- Display ---------------------------------------------------------- */
#define CB_LCD_SPI          spi1
#define CB_LCD_SCK_PIN      10
#define CB_LCD_MOSI_PIN     11
#define CB_LCD_CS_PIN       8
#define CB_LCD_DISP_PIN     22
#define CB_LCD_VCOM_PIN     23
/* Datasheet: typ 1 MHz, max 2 MHz. The Linux driver runs it at 4 MHz. */
#ifndef CB_LCD_SPI_HZ
#define CB_LCD_SPI_HZ       2000000
#endif
/* EXTCOMIN half-period in microseconds (datasheet: 1..140 Hz) */
#define CB_LCD_VCOM_US      250000

/* ---- Keyboard --------------------------------------------------------- */
#define CB_KBD_I2C          i2c1
#define CB_KBD_SDA_PIN      2
#define CB_KBD_SCL_PIN      3
#define CB_KBD_ADDR         0x1F
#define CB_KBD_I2C_HZ       100000
#define CB_KBD_POLL_US      10000

/* ---- API -------------------------------------------------------------- */

/* core0: bring up the panel and the text console (before core1 starts) */
extern void cb_lcd_init(void);
/* core0: terminal output */
extern void cb_lcd_putc(uint8_t devn, uint8_t c);
extern ttyready_t cb_lcd_ready(uint8_t devn);
extern void cb_lcd_sleeping(uint8_t devn);
/* core0: keyboard input, -1 when empty */
extern int cb_kbd_getc(uint8_t devn);

/* core1: one-time init and periodic work (panel refresh, VCOM, keyboard) */
extern void cb_core1_init(void);
extern void cb_core1_poll(void);

/* shared between lcdcon.c and bbkbd.c */
extern void cb_kbd_core1_init(void);
extern void cb_kbd_core1_poll(void);

#endif
