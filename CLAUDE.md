# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ESP32-based transit departure display that fetches real-time data from Prague's Golemio API. Single-file Arduino/PlatformIO project for Adafruit MatrixPortal ESP32-S3 with HUB75 LED matrix panels (128x32 display).

**Key Features:**
- Standalone operation with direct API access
- WiFi captive portal for configuration
- Persistent settings in ESP32 NVS flash
- Custom 8-bit ISO-8859-2 GFXfonts with full Czech character support
- UTF-8 to ISO-8859-2 automatic conversion for API data
- Configurable minimum departure time filter
- Web-based configuration interface

## Build & Development Commands

### Building and Uploading
```bash
# Build the project
pio run

# Upload to device
pio run -t upload

# Build and upload in one command
pio run -t upload

# Clean build files
pio run -t clean

# Monitor serial output (115200 baud)
pio device monitor

# Upload and monitor
pio run -t upload && pio device monitor
```

### Debugging
```bash
# Monitor with exception decoder (configured in platformio.ini)
pio device monitor

# Check project configuration
pio project config

# List connected devices
pio device list
```

## Architecture

### Single-File Structure (src/main.cpp)

The entire application is contained in one file (~1160 lines), organized into logical sections:

1. **Configuration Management (lines 52-184)**: NVS-based persistent storage for WiFi credentials, API keys, stop IDs
2. **Display System (lines 189-468)**: HUB75 matrix driver with custom Adafruit GFXfonts for Czech character support
3. **API Integration (lines 472-610)**: Golemio API client with JSON parsing (ArduinoJson, 8KB buffer)
4. **Web Server (lines 614-959)**: Configuration UI with captive portal support
5. **WiFi Management (lines 852-1013)**: Auto-fallback to AP mode on connection failure
6. **Main Loop (lines 1079-1160)**: Event-driven updates for display, API calls, and web serving

### State Machine

The device operates in two modes:
- **AP Mode** (`apModeActive=true`): Creates WiFi network for setup, DNS captive portal active, display shows credentials
- **STA Mode** (`apModeActive=false`): Connects to configured WiFi, fetches departures every 30s (configurable), serves web UI

Transitions:
- Boot → Try STA mode → If fail (20 attempts/~10s) → AP mode
- AP mode + config save → Restart → Try STA mode
- STA mode connection loss → Auto-reconnect attempts every 30s

### Display Rendering System

- **Row-based layout**: 4 rows × 8 pixels each on 128×32 matrix
  - Rows 0-2: Departure entries (line number, destination, ETA)
  - Row 3: Date/time status bar with comma separator (e.g., "Mon, Feb 15 14:35")
- **Color coding**: Line numbers have dedicated colors (Metro A=green, B=yellow, C=red, etc.)
- **Smart text colors**: Automatically uses black text on bright backgrounds (white, yellow, green, orange, cyan, red) and white text on dark backgrounds (blue, purple, black)
- **Centered line numbers**: Text is dynamically centered within background rectangles using `getTextBounds()` with proper x1 offset compensation
- **Custom 8-bit ISO-8859-2 GFXfonts**:
  - `DepartureMono4pt8b` (small font) - Used for compact text, line numbers, status
  - `DepartureMono5pt8b` (medium font) - Used for destinations, larger text
  - Full ISO-8859-2 character set (0x20-0xDF) including all Czech diacritics
  - Located in `/fonts` directory
- **UTF-8 Conversion**: API responses in UTF-8 are automatically converted to ISO-8859-2 encoding using in-place conversion (`utf8tocp()`)
- **Non-blocking updates**: `isDrawing` flag prevents concurrent display access

### Memory Management

- Display DMA buffer allocated at startup (HUB75_I2S_CFG)
- JSON deserialization uses 8KB DynamicJsonDocument
- Typical free heap: ~200KB
- NVS flash used for configuration persistence
- No dynamic allocation in main loop

### Pin Configuration

HUB75 matrix pins are hardcoded for Adafruit MatrixPortal ESP32-S3 (lines 25-40). RGB data pins (R1/G1/B1/R2/G2/B2), address pins (A/B/C/D/E), and control pins (LAT/OE/CLK) must match hardware layout.

## API Integration Details

### Golemio API
- Endpoint: `https://api.golemio.cz/v2/pid/departureboards`
- Authentication: `x-access-token` header (get key at api.golemio.cz/api-keys)
- Query parameters: `ids` (comma-separated stop IDs), `total`, `minutesBefore`, `minutesAfter`
- Response format: JSON with stops array and departures array
- Rate limits: Configurable refresh interval (10-300s) to avoid HTTP 429

### Stop ID Format
- GTFS IDs from PID data (e.g., "U693Z2P")
- Multiple stops supported via comma separation in config.stopIds
- Find IDs at: http://data.pid.cz/stops/xml/StopsByName.xml

### Departure Data Structure
```cpp
struct Departure {
    char line[8];           // Route short name
    char destination[32];   // Trip headsign
    int eta;                // Calculated from predicted/scheduled timestamp
    bool hasAC;             // trip.is_air_conditioned
    bool isDelayed;         // From delay.minutes field
    int delayMinutes;
}
```

## Hardware Constraints

- **Target board**: `adafruit_matrixportal_esp32s3` (PlatformIO board definition)
- **WiFi**: 2.4GHz only (ESP32-S3 limitation)
- **Display**: 2× HUB75 64×32 panels chained (128×32 total resolution)
- **Memory**: Default partition scheme, ~200KB typical free heap
- **Clock speed**: 10MHz I2S for HUB75 communication
- **USB CDC**: Enabled on boot for serial debugging

## Web Interface Routes

- `GET /` - Main dashboard with status and config form
- `POST /save` - Save configuration (triggers restart if WiFi changed)
- `POST /refresh` - Force immediate API call
- `POST /reboot` - Device restart
- Captive portal detection: `/generate_204`, `/hotspot-detect.html`, `/ncsi.txt`, `/success.txt`
- `404 handler` - Redirects to root (captive portal behavior)

## Configuration Storage

NVS namespace: "transport"
- `wifiSsid` (String)
- `wifiPass` (String)
- `apiKey` (String)
- `stopIds` (String, comma-separated)
- `refresh` (Int, seconds, 10-300)
- `numDeps` (Int, 1-6)
- `minDepTime` (Int, minutes, 0-30) - Filter out departures below this time
- `configured` (Bool)

Defaults: WiFi from DEFAULT_WIFI_SSID/PASSWORD defines, 30s refresh, 3 departures, 3min minimum departure time

## Time Handling

- NTP sync: `pool.ntp.org`
- Timezone: CET/CEST (UTC+1/+2)
- ETA calculation: Compares ISO timestamp from API with local time (mktime/difftime)
- Display format: "Wed 08.Jan 14:35" on bottom row

## Font System

### Custom 8-bit ISO-8859-2 GFXfonts

Located in `/fonts` directory:
- **8-bit fonts (ISO-8859-2 encoding)**:
  - `DepartureMono4pt8b.h` - Small font (4pt)
  - `DepartureMono5pt8b.h` - Medium font (5pt)
  - Character range: 0x20-0xDF (192 printable characters)
  - Full ISO-8859-2 support for Czech, Slovak, Polish, Hungarian, etc.

**UTF-8 Conversion System:**
Located in `/src` directory:
- `decodeutf8.cpp/h` - UTF-8 decoder (based on RFC 3629)
- `gfxlatin2.cpp/h` - Converts UTF-8 to ISO-8859-2 with GFX encoding (characters 0xA0-0xFF shifted to 0x80-0xDF)

**Usage in Code:**
```cpp
#include "../fonts/DepartureMono5pt8b.h"
#include "gfxlatin2.h"

const GFXfont* fontMedium = &DepartureMono5pt8b;

// Get UTF-8 string from API
char destination[32];
strlcpy(destination, "Nádraží Hostivař", sizeof(destination));

// Convert to ISO-8859-2 (in-place)
utf8tocp(destination);

// Display with proper Czech characters
display->setFont(fontMedium);
display->setTextColor(COLOR_WHITE);
display->setCursor(x, y);
display->print(destination);  // Correctly shows "ř" and other diacritics
```

**Font API (Adafruit GFX):**
- `display->setFont(const GFXfont*)` - Switch font
- `display->setTextColor(uint16_t)` - Set foreground color (transparent background)
- `display->setCursor(int16_t x, int16_t y)` - Position cursor
- `display->getTextBounds(const char*, int16_t, int16_t, int16_t*, int16_t*, uint16_t*, uint16_t*)` - Measure text dimensions
- `display->print(const char*)` - Render text

All fonts are stored in PROGMEM to save RAM.

**Font Generation:**
8-bit fonts are generated using the [fontconvert8-iso8859-2](https://github.com/petrbrouzda/fontconvert8-iso8859-2) tool with ISO-8859-2 encoding, which shifts extended characters (0xA0-0xFF) by -32 to fit in the 0x80-0xDF range, allowing full 8-bit character coverage.
