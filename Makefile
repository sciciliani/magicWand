# Magic Wand — common tasks. Run `make help`.
#
# Primary board: Seeed XIAO MG24 Sense (firmware/MagicWand)
# Previous board: XIAO nRF52840 Sense (firmware/MagicWand_nRF52840)
# The gesture engine lives in firmware/common and is copied into each sketch
# (Arduino only compiles files inside the sketch folder): run `make sync`.

SKETCH    := firmware/MagicWand
SKETCH_NRF:= firmware/MagicWand_nRF52840
# VERIFY with: arduino-cli board listall | grep -i xiao
FQBN      ?= SiliconLabs:silabs:xiao_mg24
# VERIFY with: arduino-cli board details -b $(FQBN)   (look for the protocol stack option)
BOARD_OPTS?= --board-options "protocol_stack=ble_arduino"
FQBN_NRF  ?= Seeeduino:nrf52:xiaonRF52840Sense
PORT      ?= $(shell ls /dev/cu.usbmodem* /dev/ttyACM* 2>/dev/null | head -1)
SILABS_URL:= https://siliconlabs.github.io/arduino/package_arduinosilabs_index.json
SEEED_URL := https://files.seeedstudio.com/arduino/package_seeeduino_boards_index.json
CXX       ?= g++
CHECK     := $(CXX) -std=gnu++17 -fsyntax-only -Wall -Wextra -Wno-unused-parameter -x c++ -include Arduino.h

.PHONY: help sync test test-gesture test-ir check-fw setup compile upload monitor app clean

help:
	@echo "make sync       copy firmware/common/gesture.* into both sketches"
	@echo "make test       host tests: gesture engine (grip invariance) + IR encoders"
	@echo "make check-fw   host compile-check of both firmwares against stub headers"
	@echo "make setup      install the Silicon Labs core + LSM6DS3 library (arduino-cli)"
	@echo "make compile    build for the XIAO MG24 Sense"
	@echo "make upload     build + flash (PORT=$(PORT))"
	@echo "make monitor    serial monitor at 115200 (type protocol commands, e.g. HELLO)"
	@echo "make app        serve the web app at http://localhost:8000 (open in Chrome)"

sync:
	cp firmware/common/gesture.h firmware/common/gesture.cpp $(SKETCH)/
	cp firmware/common/gesture.h firmware/common/gesture.cpp $(SKETCH_NRF)/

test: test-gesture test-ir test-ir-codec
	@for s in $(SKETCH) $(SKETCH_NRF); do \
	  cmp -s firmware/common/gesture.cpp $$s/gesture.cpp && cmp -s firmware/common/gesture.h $$s/gesture.h \
	  || { echo "$$s/gesture.* differs from firmware/common: run 'make sync'"; exit 1; }; done

build/test_gesture: test/test_gesture.cpp firmware/common/gesture.cpp firmware/common/gesture.h
	@mkdir -p build
	$(CXX) -O2 -Wall -Wextra -std=c++17 test/test_gesture.cpp firmware/common/gesture.cpp -o $@

test-gesture: build/test_gesture
	./build/test_gesture

test-ir:
	node test/test_ir.mjs

build/test_ir_codec: test/test_ir_codec.cpp $(SKETCH)/ir.cpp $(SKETCH)/ir.h $(SKETCH)/config.h
	@mkdir -p build
	$(CXX) -O1 -Wall -std=c++17 -Itest/stubs_mg24 -I$(SKETCH) test/test_ir_codec.cpp -o $@

test-ir-codec: build/test_ir_codec
	node test/dump_presets.mjs > build/presets.txt
	./build/test_ir_codec build/presets.txt

# Catches typos/type errors without the ARM toolchain. NOT a substitute for `make compile`.
check-fw:
	@set -e; for f in $(SKETCH)/*.cpp $(SKETCH)/*.ino; do echo "  mg24  $$f"; $(CHECK) -Itest/stubs_mg24 $$f; done
	@set -e; for f in $(SKETCH_NRF)/*.cpp $(SKETCH_NRF)/*.ino; do echo "  nrf52 $$f"; $(CHECK) -Itest/stubs_nrf52840 $$f; done
	@echo "firmware syntax OK"

setup:
	arduino-cli config init --overwrite --additional-urls $(SILABS_URL),$(SEEED_URL)
	arduino-cli core update-index
	arduino-cli core install SiliconLabs:silabs
	arduino-cli lib install "Seeed Arduino LSM6DS3"

compile:
	arduino-cli compile --fqbn $(FQBN) $(BOARD_OPTS) $(SKETCH)

upload: compile
	arduino-cli upload --fqbn $(FQBN) $(BOARD_OPTS) -p $(PORT) $(SKETCH)

monitor:
	arduino-cli monitor -p $(PORT) -c baudrate=115200

app:
	@echo "Open http://localhost:8000 in Chrome (or http://localhost:8000/?demo without a wand)"
	python3 -m http.server 8000 -d app

clean:
	rm -rf build
