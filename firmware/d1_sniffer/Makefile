# D1 Sniffer — Waveshare ESP32-S3-RS485-CAN
#
# Requires arduino-cli: https://arduino.github.io/arduino-cli/
#   brew install arduino-cli
#   make setup      # first time: installs esp32 board package
#
# Usage:
#   make flash          # compile + upload
#   make monitor        # open serial monitor (115200 baud)
#   make flash-monitor  # flash then immediately open monitor
#   make PORT=/dev/cu.usbserial-XXXX flash   # override port
#
# Note: this sketch uses PartitionScheme=default (no OTA).
# The main firmware (D2+) uses default_ota — see firmware/hottub/Makefile.

ARDUINO_CLI := arduino-cli
BOARD_PKG   := esp32:esp32

# Waveshare ESP32-S3-RS485-CAN:
#   - ESP32-S3 chip
#   - 8MB flash, QIO mode
#   - USB CDC enabled (so the USB port shows up as a serial port after boot)
#   - No OTA partition needed for this diagnostic sketch
FQBN := esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashMode=qio80m,FlashSize=8M,PartitionScheme=default

BAUD := 115200

# Auto-detect the first connected ESP32-S3 USB port.
# Override with: make PORT=/dev/cu.usbserial-XXXX flash
PORT ?= $(shell ls /dev/cu.usbmodem* /dev/cu.SLAB_USBtoUART* 2>/dev/null | head -1)

# ─────────────────────────────────────────────────────────────────────────────

.PHONY: all setup flash monitor flash-monitor clean

all: flash

# Install the ESP32 board package (run once)
setup:
	$(ARDUINO_CLI) core update-index
	$(ARDUINO_CLI) core install $(BOARD_PKG)

flash:
	@test -n "$(PORT)" || { echo "\nERROR: No ESP32 port detected."; \
	  echo "  Plug in the board, or set PORT explicitly:"; \
	  echo "  make PORT=/dev/cu.usbserial-XXXX flash\n"; exit 1; }
	$(ARDUINO_CLI) compile --fqbn $(FQBN) .
	$(ARDUINO_CLI) upload -p $(PORT) --fqbn $(FQBN) .

monitor:
	@test -n "$(PORT)" || { echo "\nERROR: No port detected.\n"; exit 1; }
	$(ARDUINO_CLI) monitor -p $(PORT) --config baudrate=$(BAUD)

flash-monitor: flash monitor

clean:
	rm -rf build/
