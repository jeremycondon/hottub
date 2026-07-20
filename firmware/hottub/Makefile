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

# The device flashes the image as one non-resumable stream, and espota aborts
# the whole upload if any single 1024B chunk isn't ACKed within its hardcoded
# 10s window. On the marginal RF inside the enclosure a stall is probabilistic,
# so we just re-run the (idempotent) upload until one attempt lands. -r shows a
# live progress bar so you can see how far each attempt gets.
OTA_RETRIES ?= 8

ota:
	$(ARDUINO_CLI) compile --profile $(PROFILE) --output-dir build/ .
	@ESPOTA=$$(find ~/Library/Arduino15/packages/esp32 ~/.arduino15/packages/esp32 -name espota.py 2>/dev/null | head -1); \
	AUTH=$$(sed -n 's/.*OTA_PASSWORD[[:space:]]*"\(.*\)".*/\1/p' secrets.h); \
	BIN=$$(ls build/*.ino.bin | head -1); \
	n=0; \
	while [ $$n -lt $(OTA_RETRIES) ]; do \
	  n=$$((n+1)); \
	  printf '\n=== OTA attempt %d/%d -> %s ===\n' $$n $(OTA_RETRIES) $(OTA_HOST); \
	  if python3 $$ESPOTA -r -i $(OTA_HOST) -p $(OTA_PORT) --auth=$$AUTH -f $$BIN; then \
	    printf '\nOTA succeeded on attempt %d\n' $$n; exit 0; \
	  fi; \
	  printf 'attempt %d failed; retrying in 2s...\n' $$n; sleep 2; \
	done; \
	printf '\nOTA failed after %d attempts\n' $(OTA_RETRIES); exit 1

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
	g++ -std=c++17 -Wall -o /tmp/ht_tc test/test_temp_convert.cpp && /tmp/ht_tc
	g++ -std=c++17 -Wall -o /tmp/ht_rc test/test_spa_reconcile.cpp spa_reconcile.cpp spa_control.cpp && /tmp/ht_rc

clean:
	rm -rf build/
