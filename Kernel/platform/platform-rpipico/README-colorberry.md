# FUZIX on a ColorBerry (Waveshare RP2040-PiZero)

This build runs FUZIX on a Waveshare RP2040-PiZero fitted into a ColorBerry
handheld in place of the Raspberry Pi Zero 2 W. The built-in colour screen and
keyboard are the console, so no computer is needed once it is flashed.

## What works

- **Screen:** JDI LPM027M128C 400x240 colour memory LCD as a 66x30 text console.
  ANSI colours, reverse, underline, cursor addressing, scroll regions, insert
  and delete line. The terminal type is `colorberry` and is set up for you on `tty1`.
- **Keyboard:** Beepy-style keyboard controller over I2C, with the Linux
  `beepy-kbd` layout and sticky modifiers.
- **Storage:** root filesystem in the on-board flash (`hda`), with an optional SD card in the
  PiZero's TF slot (`hdb`).
- **Serial:** USB serial on `tty2` and UART0 (GP0 TX on header pin 27, GP1 RX on pin 28) on `tty3` for debugging.

The screen refresh, EXTCOMIN toggling and keyboard polling run on the second
core, alongside USB.

## Keys

| Key | Sends |
|---|---|
| Shift, Alt, Sym, Call | Sticky modifiers. Tap one to apply it to the next key, or hold it. |
| Alt + letter | Symbol printed on the key (`#1 2 3 ( ) _ - + @ * 4 5 6 / : ; ' " 7 8 9 ? ! , .`) |
| Sym + letter | ``~ ` { } [ ] < > ^ % = \ \| & "`` |
| Call + letter | Control code (Call+C is ^C) |
| Back | Escape |
| Berry | Tab |
| Alt + Enter | Tab |
| Alt + Backspace | Delete (0x7F) |
| Touchpad click | Turns on arrow-key mode. Click again for Enter; Back turns it off. |

## Pin mapping (please check against your board)

The ColorBerry carrier is wired for Pi BCM pin numbers. The RP2040-PiZero
does not route every BCM n to RP2040 GPIO n, so the code uses this map, which
is collected in `colorberry.h`:

| Function | Pi header | BCM | RP2040 |
|---|---|---|---|
| LCD SCLK | 23 | 11 | GP10 (SPI1) |
| LCD SI | 19 | 10 | GP11 (SPI1) |
| LCD SCS | 24 | 8 | GP8 |
| LCD DISP | 15 | 22 | GP22 |
| LCD EXTCOMIN | 16 | 23 | GP23 |
| KBD SDA | 3 | 2 | GP2 (I2C1) |
| KBD SCL | 5 | 3 | GP3 (I2C1) |
| SD card | on-board | - | GP18 SCK, GP19 MOSI, GP20 MISO, GP21 CS |

This mapping has been checked against the GPxx labels printed on the back of
the RP2040-PiZero.

Other notes from the RP2040-PiZero schematic:

- The keyboard I2C pins (GP2/GP3) have no pull-up resistors on the PiZero.
  A real Pi Zero has 1.8k pull-ups there, so the ColorBerry may rely on them.
  FUZIX turns on the RP2040's weak internal pull-ups. If the keyboard does not
  respond, fit 2.2k-4.7k resistors from pins 3 and 5 to 3.3V.
- GP2/GP3 and GP22/GP23 also go to the mini-HDMI socket through series
  resistors. Leave HDMI unplugged while running in the ColorBerry.
- Header 5V (pins 2/4) is the same net as the PiZero's USB-C VBUS and feeds
  its battery charger. Do not also fit a battery to the PiZero's own battery
  connector while it is inside the ColorBerry.

Known limitation: header pin 12 (BCM18, the ColorBerry backlight) is the
same net as the on-board SD clock, GP18. The backlight is therefore not
controlled by FUZIX, and it may flicker while the SD card is in use.

## Building

```
make TARGET=rpipico SUBTARGET=colorberry image
```

Run it from `Kernel/platform/platform-rpipico`. If you built for another subtarget
before, run `rm -rf build` first. The build produces:

- `build/fuzix.uf2`: the kernel
- `filesystem.uf2`: the root filesystem for the on-board flash
- `filesystem.img`: the same filesystem, for writing to an SD card partition

User memory is 148 KB instead of 160 KB, because the console needs some of
the RAM.

## Installing

1. Hold BOOT on the PiZero, plug in USB and copy `fuzix.uf2` to the drive.
2. Do the same with `filesystem.uf2`.
3. At the `bootdev:` prompt, type `hda` and press Enter (or `hdb1` to boot
   from the first partition of an SD card).
4. Log in as `root` (no password).
