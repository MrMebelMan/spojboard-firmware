# Using Generic ESP32-S3 Boards

This guide explains how to adapt SpojBoard firmware for generic ESP32-S3 development boards instead of the Adafruit MatrixPortal ESP32-S3.

## Overview

The SpojBoard firmware can run on any ESP32-S3 board with sufficient GPIO pins and flash memory. The main differences are pin configuration and physical wiring - the software architecture remains the same.

## Advantages

✅ **Lower cost** - Generic boards cost $5-15 vs MatrixPortal $25-30
✅ **Greater flexibility** - Choose any pin layout that suits your project
✅ **More flash options** - Boards available with 4MB, 8MB, 16MB, or 32MB flash
✅ **Wider availability** - Multiple vendors and form factors

## Disadvantages

❌ **Custom wiring required** - No plug-and-play HUB75 connector
❌ **Pin mapping complexity** - Must choose appropriate GPIO pins
❌ **More complex assembly** - Requires careful planning and documentation
❌ **Potential level shifting** - Some displays may need 5V logic level shifters

## Requirements

### Hardware Requirements

- ESP32-S3 development board with **at least 8MB flash** (for OTA updates)
- **13 available GPIO pins** for HUB75 interface
- USB-C cable for programming
- External 5V power supply (see main README for details)

### Recommended Boards

- **ESP32-S3-DevKitC-1** (8MB/16MB flash version)
- **LOLIN S3** (16MB flash)
- **ESP32-S3-WROOM** based boards
- Any ESP32-S3 board with ≥8MB flash and 13+ free GPIOs

## Firmware Modifications

### 1. PlatformIO Board Configuration

**File:** `platformio.ini`

**Change the board definition:**

```ini
[env:esp32s3]
platform = espressif32
board = esp32-s3-devkitc-1  # or your specific board
framework = arduino

; Rest of configuration remains the same
board_build.partitions = partitions_custom.csv
lib_deps = ...
```

**Common board options:**
- `esp32-s3-devkitc-1` - Official Espressif DevKit
- `lolin_s3` - LOLIN S3
- `esp32s3box` - ESP32-S3-Box

### 2. GPIO Pin Mapping

**File:** `src/config/AppConfig.h` (lines 31-47)

Replace the MatrixPortal pin definitions with your chosen GPIO pins:

```cpp
// Pin Mapping for Generic ESP32-S3
#define R1_PIN 1
#define G1_PIN 2
#define B1_PIN 3
#define R2_PIN 4
#define G2_PIN 5
#define B2_PIN 6

#define A_PIN 7
#define B_PIN 8
#define C_PIN 9
#define D_PIN 10
#define E_PIN 11

#define LAT_PIN 12
#define OE_PIN 13
#define CLK_PIN 14
```

**Pin Selection Guidelines:**

**Safe to use:**
- GPIO 1-18 (most are safe for general use)
- GPIO 21
- GPIO 33-48

**Avoid these pins:**
- GPIO 0 (Boot button - may cause issues)
- GPIO 19, 20 (USB D-/D+)
- GPIO 26-32 (SPI flash/PSRAM - board dependent)
- Check your board schematic for strapping pins

### 3. USB CDC Configuration (Optional)

Some generic ESP32-S3 boards use USB-to-UART chips (CH340, CP2102) instead of native USB.

**If your board has native USB CDC:**
Keep the flag in `platformio.ini`:
```ini
-DARDUINO_USB_CDC_ON_BOOT=1
```

**If your board uses USB-to-UART chip:**
Remove or comment out the flag:
```ini
; -DARDUINO_USB_CDC_ON_BOOT=1
```

### 4. Flash Size Verification

The custom partition table (`partitions_custom.csv`) requires **8MB flash minimum**:
- 2MB for app0 (running firmware)
- 2MB for app1 (OTA update slot)
- ~4MB for SPIFFS (future use)

**If your board has less than 8MB flash:**
1. You'll need to modify `partitions_custom.csv` to reduce partition sizes
2. OTA updates may become difficult or impossible with <4MB flash
3. Consider upgrading to a board with 8MB+ flash

**To check your board's flash size:**
```bash
pio run -t upload -v
# Look for "Flash Size: XMB" in output
```

## Physical Wiring

### HUB75 Connector Pinout

Connect 13 signal pins from ESP32-S3 to HUB75 panel input connector:

| Signal | Function |
|--------|----------|
| R1, G1, B1 | Upper half RGB data |
| R2, G2, B2 | Lower half RGB data |
| A, B, C, D, E | Row address (5 bits for 1/32 scan) |
| LAT | Latch/strobe |
| OE | Output enable (active low) |
| CLK | Pixel clock |

**Power:** Both ESP32-S3 and HUB75 panels connect to external 5V supply. See main README for power requirements.

### Level Shifting Considerations

ESP32-S3 outputs 3.3V logic. Most HUB75 panels work fine with 3.3V signals, but some panels may require 5V logic levels.

**Test without level shifters first:**
- If display looks bright and stable → you're good
- If display is dim, flickering, or unstable → add level shifters

**If level shifters are needed:**
- Use 74HCT245 octal bus transceivers
- You'll need 2 chips (16 channels) for all 13 signals
- Connect 3.3V from ESP32-S3 to A side, 5V to B side
- Set direction control to A→B (ESP32 to HUB75)

## Quick Start

1. **Update firmware configuration:**
   - Modify `platformio.ini` board definition
   - Update GPIO pin mapping in `src/config/AppConfig.h`

2. **Flash and test:**
   - Flash firmware to ESP32-S3
   - Test WiFi connectivity via AP mode

3. **Connect display:**
   - Wire 13 signal pins from ESP32-S3 to HUB75 connector
   - Connect external 5V power to both ESP32-S3 and HUB75 panels
   - Power on and verify display operation

## Troubleshooting

### Display doesn't light up
- Verify external 5V power supply is connected
- Check that all 13 signal pins are correctly wired
- Ensure E pin is connected (required for 1/32 scan 64-row panels)

### Display shows garbage/random pixels
- Pin mapping in `AppConfig.h` doesn't match physical wiring
- Verify each GPIO pin definition corresponds to correct HUB75 signal

### Display is dim or flickering
- Try adding 74HCT245 level shifters to boost 3.3V signals to 5V
- Verify power supply has sufficient current capacity

### OTA updates fail
- Flash size must be ≥8MB for dual OTA partitions
- Verify partition table in `partitions_custom.csv` matches your flash size

## Example Pin Mapping

For ESP32-S3-DevKitC-1:

```cpp
#define R1_PIN 1
#define G1_PIN 2
#define B1_PIN 3
#define R2_PIN 4
#define G2_PIN 5
#define B2_PIN 6
#define A_PIN 7
#define B_PIN 8
#define C_PIN 9
#define D_PIN 10
#define E_PIN 11
#define LAT_PIN 12
#define OE_PIN 13
#define CLK_PIN 14
```

This uses consecutive low-numbered GPIOs for easy breadboard wiring. Adjust as needed for your specific board and layout.
