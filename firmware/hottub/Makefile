# Main Firmware — Waveshare ESP32-S3-RS485-CAN
#
# NOTE: This sketch has no hottub.ino yet. When you start the firmware, add a
# sketch.yaml here to pin the platform + library versions (HomeSpan, WiFiManager,
# etc.) for reproducible OTA builds — copy the pattern from
# ../d1_sniffer/sketch.yaml and switch the targets here to `--profile`.
#
# Requires arduino-cli: brew install arduino-cli
#   make setup      # first time: installs board package + libraries
#
# Usage:
#   make flash          # compile + upload via USB
#   make ota            # upload via ArduinoOTA (over WiFi)
#   make monitor        # open serial monitor
#   make flash-monitor  # flash then monitor
#
# OTA hostname: hottub.local  (after D4 is implemented)

ARDUINO_CLI := arduino-cli
BOARD_PKG   := esp32:esp32

# PartitionScheme=default_ota — two app slots (OTA_0 + OTA_1) for A/B OTA,
# plus a small SPIFFS partition for config/logs.
# With 8MB flash this gives ~1.9MB per app slot and ~1.5MB SPIFFS.
FQBN := esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashMode=qio80m,FlashSize=8M,PartitionScheme=default_ota

BAUD     := 115200
OTA_HOST := hottub.local
OTA_PORT := 3232

PORT ?= $(shell ls /dev/cu.usbmodem* /dev/cu.SLAB_USBtoUART* 2>/dev/null | head -1)

LIBS := HomeSpan WiFiManager

# ─────────────────────────────────────────────────────────────────────────────

.PHONY: all setup flash ota monitor flash-monitor clean

all: flash

setup:
	$(ARDUINO_CLI) core update-index
	$(ARDUINO_CLI) core install $(BOARD_PKG)
	$(ARDUINO_CLI) lib install $(LIBS)

flash:
	@test -n "$(PORT)" || { echo "\nERROR: No ESP32 port detected."; \
	  echo "  Plug in the board, or set PORT explicitly:"; \
	  echo "  make PORT=/dev/cu.usbserial-XXXX flash\n"; exit 1; }
	$(ARDUINO_CLI) compile --fqbn $(FQBN) .
	$(ARDUINO_CLI) upload -p $(PORT) --fqbn $(FQBN) .

# OTA upload — board must be on WiFi and running firmware with ArduinoOTA enabled
ota:
	$(ARDUINO_CLI) compile --fqbn $(FQBN) .
	$(ARDUINO_CLI) upload --fqbn $(FQBN) \
	  -p $(OTA_HOST) \
	  --protocol network \
	  --upload-field port=$(OTA_PORT) .

monitor:
	@test -n "$(PORT)" || { echo "\nERROR: No port detected.\n"; exit 1; }
	$(ARDUINO_CLI) monitor -p $(PORT) --config baudrate=$(BAUD)

flash-monitor: flash monitor

clean:
	rm -rf build/
