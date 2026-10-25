# Set one of these depending.
export PICO_SDK_FETCH_FROM_GIT = yes
#export PICO_SDK_PATH = ${HOME}/.pico-sdk/sdk/1.5.1

# Set 'SUBTARGET' to one of pico-sdk compatible boards
# valid values: pico or pico2
ifndef SUBTARGET
SUBTARGET=pico # set default subtarget
endif

ifeq ($(SUBTARGET),pico)
TOTALMEM=160
endif
ifeq ($(SUBTARGET),pico_w)
TOTALMEM=160
endif
ifeq ($(SUBTARGET),pico2)
TOTALMEM=320
endif
ifeq ($(SUBTARGET),pico2_w)
TOTALMEM=320
endif

# Waveshare RP2040-PiZero in a ColorBerry handheld (LCD + keyboard console)
PICO_BOARD=${SUBTARGET}
CMAKE_EXTRA=
FTL_KB=1952
# SUBTARGET=colorberry      : Waveshare RP2040-PiZero
# SUBTARGET=colorberry2350  : Waveshare RP2350-PiZero
ifeq ($(SUBTARGET),colorberry2350)
CB_BOARD=waveshare_rp2350_pizero
CB_MEM=312
endif
ifeq ($(SUBTARGET),colorberry)
CB_BOARD=waveshare_rp2040_pizero
CB_MEM=148
endif
ifneq ($(CB_BOARD),)
PICO_BOARD=$(CB_BOARD)
TOTALMEM=$(CB_MEM)
CMAKE_EXTRA=-DCOLORBERRY=ON
# 16MB flash minus the 96K kernel area: must match devflash.c's geometry
FTL_KB=16288
# 8MB root filesystem with 1024 inodes; the rest is FTL spare space
export FSSIZE=16384
export ISIZE=128
# make SUBTARGET=colorberry CB_SD=1 : use the SD card, no backlight control
ifeq ($(CB_SD),1)
CMAKE_EXTRA+=-DCB_USE_SD=ON
else
CMAKE_EXTRA+=-DCB_USE_SD=OFF
endif
# make SUBTARGET=colorberry CB_FONT=8x16 : console font (see cb_term.h)
CB_FONT ?= 6x8
CB_TEXT_4x8=100 30
CB_TEXT_6x8=66 30
CB_TEXT_8x8=50 30
CB_TEXT_8x16=50 15
CB_TEXT_12x16=33 15
CB_TEXT_16x16=25 15
CB_TEXT=$(CB_TEXT_$(CB_FONT))
ifeq ($(CB_TEXT),)
$(error CB_FONT must be one of 4x8 6x8 8x8 8x16 12x16 16x16)
endif
CMAKE_EXTRA+=-DCB_FONT=$(CB_FONT)
# columns and rows for the termcap entry and getty (update-flash.sh)
export CB_COLS=$(word 1,$(CB_TEXT))
export CB_ROWS=$(word 2,$(CB_TEXT))
# CMake caches its options: when CB_SD or CB_FONT change, delete
# build/Makefile so cmake runs again with the new values
CB_STAMP=sd=$(CB_SD) font=$(CB_FONT)
ifneq ($(CB_STAMP),$(shell cat build/.colorberry 2>/dev/null))
$(shell rm -f build/Makefile)
endif
endif
export SUBTARGET

PICOTOOLFLAGS= --family rp2040
ifeq (${SUBTARGET}, pico2)
PICOTOOLFLAGS= --family data
endif
ifeq (${SUBTARGET}, colorberry2350)
PICOTOOLFLAGS= --family data
endif

PICOTOOL=picotool

ifeq (, $(shell which picotool))
PICOTOOL=build/_deps/picotool/picotool
endif

include ../../../version.mk

build/fuzix.elf: ../../version.c build/Makefile $(wildcard *.[chS])
	$(MAKE) -C build

../../version.c:
	$(MAKE) -C ../.. TARGET=rpipico version.c

build/Makefile: CMakeLists.txt $(wildcard ../../*.[chS]) $(wildcard ../../*/*.[chS])
	mkdir -p build
	(cd build && cmake -DPICO_BOARD=${PICO_BOARD} -DTOTALMEM=${TOTALMEM} ${CMAKE_EXTRA} ..)
ifneq ($(CB_BOARD),)
	echo "$(CB_STAMP)" > build/.colorberry
endif

image:: world
	./update-flash.sh
	../../../Standalone/mkftl -s ${FTL_KB} -e 0x1000 -g 10 filesystem.img -o filesystem.ftl
	${PICOTOOL} uf2 convert filesystem.ftl -t bin filesystem.uf2 -o 0x10018000 ${PICOTOOLFLAGS}

diskimage:

clean:
	rm -rf build
	rm -f uf2conv
	$(MAKE) -C ../../../Library/libs -f Makefile.armm0 clean
	$(MAKE) -C ../../../Applications/util -f Makefile.armm0 clean
	$(MAKE) -C ../../../Applications/V7/cmd -f Makefile.armm0 clean
	$(MAKE) -C ../../../Applications/V7/cmd/sh -f Makefile.armm0 clean
	$(MAKE) -C ../../../Applications/games -f Makefile.armm0 clean
	$(MAKE) -C ../../../Applications/levee -f Makefile.armm0 clean
	$(MAKE) -C ../../../Applications/cave -f Makefile.armm0 clean
	$(MAKE) -C ../../../Applications/cursesgames -f Makefile.armm0 clean
	$(MAKE) -C ../../../Standalone clean
	$(MAKE) -C utils clean

world: build/fuzix.elf
	$(MAKE) -C ../../../Library/libs -f Makefile.armm0
	$(MAKE) -C ../../../Applications/util -f Makefile.armm0
	$(MAKE) -C ../../../Applications/V7/cmd -f Makefile.armm0
	$(MAKE) -C ../../../Applications/V7/cmd/sh -f Makefile.armm0
	$(MAKE) -C ../../../Applications/games -f Makefile.armm0
	$(MAKE) -C ../../../Applications/levee -f Makefile.armm0
	$(MAKE) -C ../../../Applications/cave -f Makefile.armm0
	$(MAKE) -C ../../../Applications/cursesgames -f Makefile.armm0
	$(MAKE) -C ../../../Standalone
	$(MAKE) -C utils
