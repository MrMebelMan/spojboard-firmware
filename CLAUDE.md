# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ESP32-based transit departure display that fetches real-time data from Prague's Golemio API. Single-file Arduino/PlatformIO project for Adafruit MatrixPortal ESP32-S3 with HUB75 LED matrix panels (128x32 display).

**Key Features:**
- Standalone operation with direct API access
- WiFi captive portal for configuration
- Persistent settings in ESP32 NVS flash
- U8g2 fonts for Czech character support
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

The entire application is contained in one file (~1167 lines), organized into logical sections:

1. **Configuration Management (lines 52-184)**: NVS-based persistent storage for WiFi credentials, API keys, stop IDs
2. **Display System (lines 189-468)**: HUB75 matrix driver with U8g2 font rendering for Czech characters
3. **API Integration (lines 472-610)**: Golemio API client with JSON parsing (ArduinoJson, 8KB buffer)
4. **Web Server (lines 614-959)**: Configuration UI with captive portal support
5. **WiFi Management (lines 852-1013)**: Auto-fallback to AP mode on connection failure
6. **Main Loop (lines 1079-1167)**: Event-driven updates for display, API calls, and web serving

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
  - Row 3: Date/time status bar
- **Color coding**: Line numbers have dedicated colors (Metro A=green, B=yellow, C=red, etc.)
- **U8g2 fonts**: `u8g2_font_5x7_tf` for line numbers, `u8g2_font_6x10_tf` for destinations
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
- `refresh` (Int, seconds)
- `numDeps` (Int, 1-6)
- `configured` (Bool)

Defaults: WiFi from DEFAULT_WIFI_SSID/PASSWORD defines, 30s refresh, 3 departures

## Time Handling

- NTP sync: `pool.ntp.org`
- Timezone: CET/CEST (UTC+1/+2)
- ETA calculation: Compares ISO timestamp from API with local time (mktime/difftime)
- Display format: "Wed 08.Jan 14:35" on bottom row
