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
ifeq ($(SUBTARGET),colorberry)
PICO_BOARD=waveshare_rp2040_pizero
TOTALMEM=148
CMAKE_EXTRA=-DCOLORBERRY=ON
# 16MB flash minus the 96K kernel area: must match devflash.c's geometry
FTL_KB=16288
# 8MB root filesystem with 1024 inodes; the rest is FTL spare space
export FSSIZE=16384
export ISIZE=128
# make SUBTARGET=colorberry CB_SD=1 : use the SD card, no backlight control
ifeq ($(CB_SD),1)
CMAKE_EXTRA+=-DCB_USE_SD=ON
endif
endif
export SUBTARGET

PICOTOOLFLAGS= --family rp2040
ifeq (${SUBTARGET}, pico2)
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
