# Main Firmware — Waveshare ESP32-S3-RS485-CAN
#
# Requires arduino-cli: brew install arduino-cli
#   make setup      # first time: installs the pinned platform + libraries
#                   # (versions are pinned in sketch.yaml)
#
# Usage:
#   make flash          # compile + upload via USB
#   make ota            # upload via ArduinoOTA (over WiFi)
#   make monitor        # open serial monitor
#   make flash-monitor  # flash then monitor
#   make test           # run host-side unit tests (no board required)
#
# OTA hostname: hottub.local  (after D4 is implemented)

ARDUINO_CLI := arduino-cli
BOARD_PKG   := esp32:esp32

# Reproducible builds: the platform + library versions are pinned in
# sketch.yaml. Building with --profile auto-installs those exact versions,
# so the FQBN and toolchain come from the profile, not from this Makefile.
PROFILE := hottub

BAUD     := 115200
OTA_HOST := hottub.local
OTA_PORT := 3232

PORT ?= $(shell ls /dev/cu.usbmodem* /dev/cu.SLAB_USBtoUART* 2>/dev/null | head -1)

# ─────────────────────────────────────────────────────────────────────────────

.PHONY: all setup flash ota monitor flash-monitor clean test

all: flash

setup:
	$(ARDUINO_CLI) core update-index
	$(ARDUINO_CLI) compile --profile $(PROFILE) .

flash:
	@test -n "$(PORT)" || { echo "\nERROR: No ESP32 port detected.\n"; exit 1; }
	$(ARDUINO_CLI) compile --profile $(PROFILE) .
	$(ARDUINO_CLI) upload -p $(PORT) --profile $(PROFILE) .

ota:
	$(ARDUINO_CLI) compile --profile $(PROFILE) --output-dir build/ .
	python3 $$(find ~/Library/Arduino15/packages/esp32 ~/.arduino15/packages/esp32 -name espota.py 2>/dev/null | head -1) \
	  -i $(OTA_HOST) -p $(OTA_PORT) --auth=$$(sed -n 's/.*OTA_PASSWORD[[:space:]]*"\(.*\)".*/\1/p' secrets.h) -f build/*.ino.bin

monitor:
	@test -n "$(PORT)" || { echo "\nERROR: No port detected.\n"; exit 1; }
	$(ARDUINO_CLI) monitor -p $(PORT) --config baudrate=$(BAUD)

flash-monitor: flash monitor

test:
	g++ -std=c++17 -Wall -o /tmp/ht_frame test/test_balboa_frame.cpp && /tmp/ht_frame
	g++ -std=c++17 -Wall -o /tmp/ht_bf test/test_build_frame.cpp && /tmp/ht_bf
	g++ -std=c++17 -Wall -o /tmp/ht_st test/test_spa_state.cpp && /tmp/ht_st
	g++ -std=c++17 -Wall -o /tmp/ht_pc test/test_spa_control.cpp spa_control.cpp && /tmp/ht_pc
	g++ -std=c++17 -Wall -o /tmp/ht_mf test/test_metrics_format.cpp && /tmp/ht_mf

clean:
	rm -rf build/
