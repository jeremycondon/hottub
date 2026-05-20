# CLAUDE.md

## Project

ESP32-S3 hot tub controller for a Balboa BP501 system. Controls via HomeKit (HomeSpan) and a custom SwiftUI iOS app. Device lives inside the hot tub enclosure — OTA updates and web-based config are mandatory, never assume physical access is easy.

## Hardware

**Board:** Waveshare ESP32-S3-RS485-CAN  
**RS485 pins:** TX=GPIO17, RX=GPIO18, TX-Enable=GPIO21 (UART1)  
**Balboa baud rate:** 115200, 8N1  
**Spa model:** Balboa BP501

## Firmware

- Framework: Arduino (ESP32 Arduino Core 3.x)
- HomeKit: HomeSpan library
- Balboa: adapted from MHotchin/BalBoaSpa
- WiFi provisioning: WiFiManager (captive portal on first boot)
- OTA: ArduinoOTA

Source is in `firmware/`. Each subdirectory is an Arduino sketch (directory name = `.ino` filename).

## iOS App

SwiftUI app in `ios/HotTub/`. Uses HomeKit framework for temp/basic controls, plus a local REST/WebSocket API to the ESP32 for spa-specific features.

## Conventions

- C++ for firmware (Arduino framework), no MicroPython
- Prefer `.h`/`.cpp` splits for anything more than ~100 lines
- No dynamic allocation in the RS485 interrupt/callback path
- All config stored in NVS (not hardcoded, not in flash image)
- Serial logging gated on a `DEBUG` flag so production builds are quiet
