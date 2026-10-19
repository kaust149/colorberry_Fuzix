# Host-side tests for the ColorBerry console (terminal emulator + keymap).
#   make        build and run both tests
#   screen.png  (needs Python + Pillow) renders the test screen
P = ../..
CFLAGS = -Wall -Wextra -Wno-unused-parameter -O1 -g -fsanitize=address,undefined \
	-DCONFIG_FONT6X8 -Istub -I$(P)

all: run

test_term: test_term.c $(P)/cb_term.c $(P)/../../font/font6x8.c
	$(CC) $(CFLAGS) -o $@ $^

test_keys: test_keys.c $(P)/cb_keys.c
	$(CC) $(CFLAGS) -o $@ $^

run: test_term test_keys
	./test_term screen.ppm
	./test_keys

screen.png: run
	python3 -c "from PIL import Image; Image.open('screen.ppm').resize((800,480), Image.NEAREST).save('screen.png')"

clean:
	rm -f test_term test_keys screen.ppm screen.png
