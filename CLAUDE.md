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

**IMPORTANT:** PlatformIO must be run from the Python virtual environment. Activate it first:
```bash
source ~/code/esp32/venv/bin/activate
```

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

### Modular Structure

The application follows a layered, modular architecture with zero circular dependencies:

```
src/
├── main.cpp                          # Application orchestration (~340 lines)
├── config/
│   ├── AppConfig.h/cpp              # Configuration structure & NVS persistence
├── display/
│   ├── DisplayManager.h/cpp         # Display rendering & layout
│   ├── DisplayColors.h/cpp          # Color system & configurable line color mapping
├── api/
│   ├── GolemioAPI.h/cpp             # API client for Golemio
│   ├── DepartureData.h/cpp          # Data structures & utilities
├── network/
│   ├── WiFiManager.h/cpp            # WiFi connection & AP mode
│   ├── CaptivePortal.h/cpp          # DNS server & captive portal
│   ├── ConfigWebServer.h/cpp        # Web interface handlers
│   ├── OTAUpdateManager.h/cpp       # OTA firmware upload handling
│   ├── GitHubOTA.h/cpp              # GitHub releases integration
└── utils/
    ├── Logger.h/cpp                 # Logging utilities
    ├── TimeUtils.h/cpp              # NTP sync & time formatting
    ├── gfxlatin2.h/cpp              # UTF-8 to ISO-8859-2 conversion
    └── decodeutf8.h/cpp             # UTF-8 decoder
```

**Key Design Principles:**
- **Layered Dependencies**: Lower layers never depend on higher layers
- **Single Responsibility**: Each module has one clear purpose
- **Callback Pattern**: Modules communicate upward via callbacks (e.g., ConfigWebServer → main.cpp)
- **Pure Data Structures**: Config passed as parameter, not stored in modules
- **Static Allocation**: No dynamic allocation in main loop for stability

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
  - Row 3: Date/time status bar with pipe separator (e.g., "Mon| Feb 15 14:35")
- **Uniform route boxes**: All line numbers displayed in 18-pixel wide black background boxes (fits 1-3 characters)
  - Route numbers horizontally centered within boxes using `getTextBounds()` with proper x1 offset compensation
  - All destinations start at fixed X position (22 pixels) for consistent vertical alignment
- **Adaptive font rendering**: Automatically switches between regular and condensed fonts for optimal display
  - Destinations ≤16 chars (or ≤15 with AC): Regular font (DepartureMono5pt8b)
  - Destinations >16 chars: Condensed font (DepartureMonoCondensed5pt8b) with 23-char capacity
  - ETA always rendered in regular font for consistency
- **Dynamic destination truncation**: Text length adjusted based on ETA display width and font choice to prevent overlap
  - Regular font: maxChars (16 without AC, 15 with AC), reduced by 1 for 2-digit ETA or 2 for 3-digit ETA
  - Condensed font: maxChars 23, reduced by 2 for 2-digit ETA or 3 for 3-digit ETA
  - Ensures destinations never overlap with ETA regardless of font used
- **Configurable line colors**: Custom color mapping system with pattern matching
  - User can configure colors via web interface (format: "LINE=COLOR,LINE=COLOR,...")
  - Supports pattern matching with trailing asterisk (e.g., "9*=CYAN" for night trams 91-99)
  - Two-pass matching: exact matches first, then patterns
  - Falls back to hardcoded defaults if no match found
  - Available colors: RED, GREEN, BLUE, YELLOW, ORANGE, PURPLE, CYAN, WHITE
  - Stored in `Config.lineColorMap[256]` field, persisted to NVS
- **Default color coding**: Hardcoded fallback colors (Metro A=green, B=yellow, C=red, S-trains=blue, night trams=cyan, etc.)
- **Custom 8-bit ISO-8859-2 GFXfonts**:
  - `DepartureMono4pt8b` (small font) - Used for compact text, line numbers, status
  - `DepartureMono5pt8b` (medium font) - Used for destinations, larger text, ETAs
  - `DepartureMonoCondensed5pt8b` (condensed font) - Automatically used for long destinations (>16 chars)
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
- **Multi-stop rate limiting**: 1-second delay (`delay(1000)`) between API calls when querying multiple stops
  - Reduces server load and prevents rate limiting
  - Applied in `GolemioAPI::fetchDepartures()` loop after each `querySingleStop()` call

### Stop ID Format
- GTFS IDs from PID data (e.g., "U693Z2P")
- Multiple stops supported via comma separation in config.stopIds
- Each stop queried individually with separate API calls (with 1s delay between calls)
- All departures collected, sorted by ETA, filtered by minimum departure time, then top N displayed
- Find IDs at: https://data.pid.cz/stops/json/stops.json

### Departure Data Structure
```cpp
struct Departure {
    char line[8];           // Route short name
    char destination[32];   // Trip headsign (with abbreviations applied)
    int eta;                // Calculated from predicted/scheduled timestamp
    bool hasAC;             // trip.is_air_conditioned
    bool isDelayed;         // From delay.minutes field
    int delayMinutes;
}
```

**Destination Abbreviations**: Long Czech words are automatically shortened to fit display:
- "Nádraží" → "Nádr." (uppercase)
- "nádraží" → "nádr." (lowercase)
- "Sídliště" → "Sídl."
- "Nemocnice" → "Nem."

Abbreviations are applied in `DepartureData.cpp` before UTF-8 conversion to preserve diacritics.

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
- `POST /update` - Handle firmware file upload (split into two handlers):
  - Upload chunk handler: `handleUpdateProgress()` - processes chunks without HTTP response
  - Completion handler: `handleUpdateComplete()` - sends final HTTP response after upload finishes
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
- `brightness` (Int, 0-255) - Display brightness level
- `lineColorMap` (String, max 256 chars) - Custom line color mappings (format: "LINE=COLOR,LINE=COLOR,...")
  - Supports pattern matching with trailing asterisk (e.g., "9*=CYAN")
  - Falls back to hardcoded defaults if empty or no match
- `configured` (Bool)

Defaults: WiFi from DEFAULT_WIFI_SSID/PASSWORD defines, 30s refresh, 3 departures, 3min minimum departure time, brightness 90, empty lineColorMap (uses hardcoded defaults)

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
  - `DepartureMono5pt8b.h` - Medium font (5pt) - default for destinations and ETAs
  - `DepartureMonoCondensed5pt8b.h` - Condensed font (5pt) - automatically used for destinations >16 chars
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
