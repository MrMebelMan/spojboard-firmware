# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**SpojBoard** - Smart Panel for Onward Journeys

ESP32-based transit departure display that fetches real-time data from Prague's Golemio API. Modular Arduino/PlatformIO project for Adafruit MatrixPortal ESP32-S3 with HUB75 LED matrix panels (128x32 display).

**SPOJ** = **S**mart **P**anel for **O**nward **J**ourneys (also "spoj" = connection/service in Czech)

**Key Features:**
- Standalone operation with direct API access
- WiFi captive portal for configuration
- Persistent settings in ESP32 NVS flash
- Custom 8-bit ISO-8859-2 GFXfonts with full Czech character support
- UTF-8 to ISO-8859-2 automatic conversion for API data
- Configurable minimum departure time filter
- Web-based configuration interface
- GitHub-based OTA firmware updates with user confirmation

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
- `GET /update` - OTA firmware upload form (manual upload)
- `POST /update` - Handle firmware file upload
- `GET /check-update` - Check GitHub for new releases (AJAX)
- `POST /download-update` - Download and install from GitHub (AJAX)
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

Located in `/src/fonts` directory:
- **8-bit fonts (ISO-8859-2 encoding)**:
  - `DepartureMono4pt8b.h` - Small font (4pt)
  - `DepartureMono5pt8b.h` - Medium font (5pt)
  - Character range: 0x20-0xDF (192 printable characters)
  - Full ISO-8859-2 support for Czech, Slovak, Polish, Hungarian, etc.

**UTF-8 Conversion System:**
Located in `/src/utils` directory:
- `decodeutf8.cpp/h` - UTF-8 decoder (based on RFC 3629)
- `gfxlatin2.cpp/h` - Converts UTF-8 to ISO-8859-2 with GFX encoding (characters 0xA0-0xFF shifted to 0x80-0xDF)

**Usage in Code:**
```cpp
#include "../fonts/DepartureMono5pt8b.h"
#include "../utils/gfxlatin2.h"

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

## OTA Update System

### Overview

SpojBoard includes two methods for firmware updates:
1. **Manual Upload**: Upload .bin file via web interface (existing)
2. **GitHub Updates**: Check for and download new releases from GitHub (new)

Both methods use the ESP32's built-in OTA partition system and are **disabled in AP mode** for security.

### GitHub OTA Update System

Located in `/src/network/GitHubOTA.{h,cpp}` - standalone class for GitHub releases integration.

**Architecture:**
```
User clicks "Check for Updates"
    → GitHubOTA::checkForUpdate()
    → GitHub API: /repos/xbach/spojboard-firmware/releases/latest
    → Parse JSON, compare versions
    → Return ReleaseInfo struct
    → Display update card in UI
User clicks "Download & Install"
    → GitHubOTA::downloadAndInstall()
    → Stream firmware from GitHub to OTA partition
    → Progress displayed on LED matrix
    → MD5 validation via Update.end(true)
    → Auto-reboot on success
```

**Key Files:**
- `/src/network/GitHubOTA.h` - Class definition with ReleaseInfo struct
- `/src/network/GitHubOTA.cpp` - Implementation with streaming download
- `/src/config/AppConfig.h` - GitHub repository constants

### GitHubOTA Class

**ReleaseInfo Struct:**
```cpp
struct ReleaseInfo {
    bool available;           // Update available?
    bool hasError;           // API error occurred?
    char errorMsg[128];      // Error message
    int releaseNumber;       // Parsed release number (1, 2, 3...)
    char tagName[32];        // GitHub tag (e.g., "r1", "r2")
    char releaseName[64];    // Human-readable name
    char releaseNotes[512];  // Truncated release body
    char assetUrl[256];      // .bin download URL
    char assetName[64];      // Filename
    size_t assetSize;        // File size in bytes
};
```

**Public Methods:**
- `ReleaseInfo checkForUpdate(const char* currentRelease)` - Query GitHub API
- `bool downloadAndInstall(const char* assetUrl, size_t expectedSize, ProgressCallback onProgress)` - Stream and flash firmware

**Private Helpers:**
- `int parseReleaseNumber(const char* tagName)` - Extract number from "r1" → 1
- `bool findBinaryAsset(JsonDocument& doc, ...)` - Find .bin file in release assets
- `bool validateFirmwareFilename(const char* filename)` - Validate pattern

### Version Comparison

**Current Version:** `FIRMWARE_RELEASE` from AppConfig.h (e.g., "1")
**GitHub Tag:** Extract from `tag_name` field (e.g., "r2" → 2)
**Logic:** Compare as integers - if GitHub version > current version, update available

**Example:**
- Current: "1"
- GitHub tag: "r2" → parsed as 2
- Result: Update available

### GitHub API Integration

**Endpoint:** `https://api.github.com/repos/xbach/spojboard-firmware/releases/latest`
**Authentication:** None (60 requests/hour unauthenticated - sufficient for manual checks)
**Timeout:** 30 seconds (HTTP_TIMEOUT_MS)
**JSON Buffer:** 8KB DynamicJsonDocument

**Response Structure:**
```json
{
  "tag_name": "r2",
  "name": "Release 2",
  "body": "## Release notes...",
  "assets": [
    {
      "name": "spojboard-r2-a1b2c3d4.bin",
      "size": 1234567,
      "browser_download_url": "https://github.com/.../download/..."
    }
  ]
}
```

### Streaming Download

**Critical Design:** Firmware (~1-2 MB) is streamed directly to OTA partition without buffering entire file in RAM.

**Flow:**
1. `HTTPClient::GET(assetUrl)` - Start download
2. `http.getStreamPtr()` - Get WiFiClient stream
3. `Update.begin(contentLength)` - Initialize OTA
4. Loop: `stream->readBytes(buffer, 1024)` → `Update.write(buffer, size)`
5. `Update.end(true)` - Finalize with MD5 validation
6. `ESP.restart()` - Reboot into new firmware

**Memory Usage:**
- Download buffer: 1KB chunks
- JSON buffer: 8KB
- GitHubOTA class: ~1KB overhead
- **Total impact: ~10KB** (acceptable with ~200KB free heap)

**Progress Updates:**
- Callback every 10KB or 1% of download
- Forwards to `DisplayManager::drawOTAProgress()`
- LED matrix shows progress bar

### Security & Validation

1. **HTTPS Only:** All communication over TLS (ESP32 built-in CA store)
2. **Filename Validation:** Regex match `spojboard-r\d+-[0-9a-f]{8}\.bin`
3. **Size Validation:** Compare Content-Length with GitHub API reported size
4. **MD5 Validation:** Automatic via `Update.end(true)` - firmware rejected if invalid
5. **AP Mode Block:** Updates disabled in AP mode (security measure)
6. **Sanity Check:** Reject if GitHub release < current release

### Error Handling

**HTTP Errors:**
- 404: No releases found
- 403: GitHub API access denied
- 429: Rate limit exceeded (60/hour)
- Timeout: Network too slow

**Download Errors:**
- Size mismatch: Content-Length ≠ expected size
- Incomplete download: Connection dropped mid-transfer
- Flash error: OTA partition write failure
- MD5 mismatch: Corrupted download

**Recovery:**
- Failed update doesn't affect running firmware (separate OTA partition)
- Device remains bootable even if update fails mid-flash
- User can retry download

### Web UI Integration

**Dashboard Button:**
```html
<button id="checkUpdateBtn">Check for Updates</button>
<div id="updateStatus"></div>
```

**JavaScript Flow:**
1. Click button → `fetch('/check-update')`
2. Parse JSON response
3. If available: Show card with version, notes, size, "Download & Install" button
4. If up-to-date: Show "You're up to date!" message
5. If error: Show error message
6. Click install → `fetch('/download-update', {method: 'POST', body: JSON})`
7. Show progress UI
8. On success: "Update installed! Rebooting..."
9. Auto-reload page after 8 seconds

**User Experience:**
- No page refresh during checking (AJAX)
- Real-time progress display
- Confirmation required before download
- Clear error messages

### Release Creation Workflow

**GitHub Actions:** `.github/workflows/release.yml`

1. **Trigger:** Push tag matching `r*` (e.g., `r1`, `r2`)
2. **Build:** PlatformIO builds firmware with timestamp-based build ID
3. **Artifact:** `dist/spojboard-r{release}-{buildid}.bin`
4. **Release:** Creates GitHub release with firmware as asset
5. **Auto-publish:** Release published automatically

**Local Build:**
```bash
./build.sh
# Output: dist/spojboard-r1-37a954fd.bin
```

### Configuration

**Hardcoded Repository:** `xbach/spojboard-firmware` (not user-configurable)

**Constants in AppConfig.h:**
```cpp
#define GITHUB_REPO_OWNER "xbach"
#define GITHUB_REPO_NAME "spojboard-firmware"
```

### Testing Updates

1. Build and deploy firmware with release "1"
2. Create GitHub release with tag "r2"
3. Upload firmware .bin as asset
4. Open device dashboard at `http://[device-ip]/`
5. Click "Check for Updates"
6. Should show update available
7. Click "Download & Install"
8. Watch progress on LED matrix
9. Device reboots with new firmware
