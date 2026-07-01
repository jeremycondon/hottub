# D1 Sniffer — Waveshare ESP32-S3-RS485-CAN
#
# Requires arduino-cli: https://arduino.github.io/arduino-cli/
#   brew install arduino-cli
#   make setup      # first time: installs the pinned platform + libraries
#                   # (versions are pinned in sketch.yaml)
#
# Usage:
#   make flash          # compile + upload via USB
#   make flash-ota      # compile + upload via OTA (WiFi)
#   make monitor        # open serial monitor (115200 baud)
#   make flash-monitor  # flash via USB then immediately open monitor
#   make PORT=/dev/cu.usbserial-XXXX flash   # override USB port
#   make OTA_HOST=192.168.1.x flash-ota      # override OTA target IP

ARDUINO_CLI := arduino-cli
BOARD_PKG   := esp32:esp32

# Reproducible builds: the platform + library versions are pinned in
# sketch.yaml. Building with --profile auto-installs those exact versions,
# so the FQBN and toolchain come from the profile, not from this Makefile.
PROFILE := d1_sniffer

# Waveshare ESP32-S3-RS485-CAN (also declared in sketch.yaml):
#   - ESP32-S3 chip
#   - 8MB flash, QIO mode
#   - USB CDC enabled (USB port appears as serial after boot)
#   - default_8MB partition scheme: two ~3.3MB OTA app slots
FQBN := esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashMode=qio,FlashSize=8M,PartitionScheme=default_8MB

BAUD := 115200

# Auto-detect the first connected ESP32-S3 USB port.
# Override with: make PORT=/dev/cu.usbserial-XXXX flash
PORT ?= $(shell ls /dev/cu.usbmodem* /dev/cu.SLAB_USBtoUART* 2>/dev/null | head -1)

# OTA target — override with: make OTA_HOST=192.168.1.x flash-ota
OTA_HOST ?= hottub-sniffer.local
OTA_PORT ?= 3232
# OTA password is read from the gitignored secrets.h (single source of truth,
# shared with the firmware). Override with: make OTA_PASS=... flash-ota
OTA_PASS ?= $(shell sed -n 's/.*OTA_PASSWORD[[:space:]]*"\(.*\)".*/\1/p' secrets.h 2>/dev/null)

# espota.py is bundled with the ESP32 Arduino core. Data dir differs by OS
# (~/Library/Arduino15 on macOS, ~/.arduino15 on Linux), so search both.
ESPOTA := $(shell find ~/Library/Arduino15/packages/esp32 ~/.arduino15/packages/esp32 -name espota.py 2>/dev/null | head -1)

# ─────────────────────────────────────────────────────────────────────────────

.PHONY: all setup libs flash flash-ota monitor flash-monitor clean

all: flash

# Install the pinned platform + libraries from sketch.yaml (run once).
# A profile compile fetches and installs the exact versions on demand, so this
# just warms the cache and the board-package index.
setup:
	$(ARDUINO_CLI) core update-index
	$(ARDUINO_CLI) compile --profile $(PROFILE) .

# Kept for ad-hoc/global installs. The pinned versions live in sketch.yaml;
# bump them there so the profile build stays the source of truth.
libs:
	$(ARDUINO_CLI) lib install "WiFiManager@2.0.17"
	$(ARDUINO_CLI) lib install "TelnetStream@1.3.0"

flash:
	@test -n "$(PORT)" || { echo "\nERROR: No ESP32 port detected."; \
	  echo "  Plug in the board, or set PORT explicitly:"; \
	  echo "  make PORT=/dev/cu.usbserial-XXXX flash\n"; exit 1; }
	$(ARDUINO_CLI) compile --profile $(PROFILE) .
	$(ARDUINO_CLI) upload -p $(PORT) --profile $(PROFILE) .

flash-ota:
	@test -n "$(ESPOTA)" || { echo "\nERROR: espota.py not found. Run 'make setup' first.\n"; exit 1; }
	$(ARDUINO_CLI) compile --profile $(PROFILE) --output-dir build/ .
	python3 $(ESPOTA) -i $(OTA_HOST) -p $(OTA_PORT) --auth=$(OTA_PASS) -f build/*.ino.bin

monitor:
	@test -n "$(PORT)" || { echo "\nERROR: No port detected.\n"; exit 1; }
	$(ARDUINO_CLI) monitor -p $(PORT) --config baudrate=$(BAUD)

flash-monitor: flash monitor

clean:
	rm -rf build/
