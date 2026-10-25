# Host-side tests for the ColorBerry console (terminal emulator + keymap).
#   make        build and run both tests
#   screen.png  (needs Python + Pillow) renders the test screen
P = ../..
# make CB_FONT=8x16 tests another console font (see cb_term.h)
CB_FONT ?= 6x8
FONT_UC = $(shell echo $(CB_FONT) | tr a-z A-Z)
FONTSRC_4X8 = font4x8.c
FONTSRC_6X8 = font6x8.c
FONTSRC_12X16 = font6x8.c
FONTSRC_8X8 = font8x8.c
FONTSRC_8X16 = font8x8.c
FONTSRC_16X16 = font8x8.c
FONTSRC = $(P)/../../font/$(FONTSRC_$(FONT_UC))
CFLAGS = -Wall -Wextra -Wno-unused-parameter -O1 -g -fsanitize=address,undefined \
	-DCONFIG_FONT6X8 -DCONFIG_FONT_4x8 -DCONFIG_FONT8X8 -DCONFIG_FONT8X8SMALL \
	-DCB_FONT_$(FONT_UC) -Istub -I$(P)

all: run

test_term: test_term.c $(P)/cb_term.c $(FONTSRC) FORCE
	$(CC) $(CFLAGS) -o $@ $(filter %.c,$^)

test_keys: test_keys.c $(P)/cb_keys.c
	$(CC) $(CFLAGS) -o $@ $(filter %.c,$^)

run: test_term test_keys
	./test_term screen.ppm
	./test_keys

screen.png: run
	python3 -c "from PIL import Image; Image.open('screen.ppm').resize((800,480), Image.NEAREST).save('screen.png')"

FORCE:
.PHONY: FORCE

clean:
	rm -f test_term test_keys screen.ppm screen.png
