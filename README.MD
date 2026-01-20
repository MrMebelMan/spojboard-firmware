# SpojBoard - Smart Panel for Onward Journeys

An LED matrix display showing real-time transit departures. Supports multiple hardware platforms and transit networks.

**SPOJ** = **S**mart **P**anel for **O**nward **J**ourneys (also "spoj" = connection/service in Czech)

| Platform | Transit Networks | Weather | Web Config | OTA Updates |
|----------|-----------------|---------|------------|-------------|
| **MatrixPortal ESP32-S3** | Prague + Berlin | ✅ | ✅ Full UI | ✅ GitHub + Manual |
| **Matrix Portal M4** | Prague only | ✅ | ❌ Hardcoded | ❌ Manual only |

Standalone operation, no external server required.

![SpojBoard Device](docs/images/spojboard-device.jpg)

**[📸 View More Screenshots](docs/SCREENSHOTS.md)**

## Documentation

- **[Architecture & Data Flow](docs/ARCHITECTURE.md)** - Technical details, pipeline flow, and design rationale
- **[Font System](docs/FONTS.md)** - Czech character support and custom font creation guide
- **[Hardware Guide for Generic ESP32-S3](docs/GENERIC_ESP32S3.md)** - Alternative hardware setup
- **[Troubleshooting](docs/TROUBLESHOOTING.md)** - Common issues and solutions

## Features

- **Multi-City Support**: Prague (Golemio API) and Berlin (BVG API) with per-city configuration
- **Weather Display**: Current temperature and conditions from Open-Meteo API (free, no API key required)
- **Direct API Access**: Fetches departure data every 10-300 seconds (configurable)
- **Local ETA Updates**: Recalculates ETAs every 10 seconds from cached timestamps without additional API calls - reduces server load while keeping display fresh
- **Extended Buffer**: Always fetches and caches 12 departures per stop for better sorting and filtering
- **Multi-Stop Support**: Query up to 12 stops simultaneously (144 departure buffer), automatically sorted by departure time with 1s rate limiting
- **WiFi Setup Portal**: Automatic AP mode with captive portal when WiFi fails - no code editing needed
- **Web Configuration**: Easy setup via built-in web interface with city selector
- **Persistent Settings**: Configuration stored in ESP32 flash memory, survives reboots
- **Extended Character Support**: Custom 8-bit ISO-8859-2 fonts with automatic UTF-8 conversion (ž, š, č, ř, ň, ť, ď, ß, etc.)
- **Adaptive Font Rendering**: Automatically switches to condensed font for long destination names (>16 chars), fitting up to 23 characters on screen
- **Headsign Shortening**: Automatic shortening of long Czech words (e.g., "Nádraží" → "Nádr.", "nádraží" → "nádr.") to maximize display space
- **Trip Filtering**: Configurable minimum departure time (default 3 minutes) to hide departures that are too soon to catch
- **AC Indicator**: Shows asterisk (*) for air-conditioned vehicles (Prague only)
- **Custom Line Colors**: Configure custom colors for specific lines via web interface with exact and pattern matching
- **Demo Mode**: Preview custom departure data on the display before API configuration - available in both setup and normal modes
- **Firmware Updates**: GitHub-based OTA updates with user confirmation - check for new releases directly from the web interface
- **Optional Telnet Logging**: Remote debugging via telnet (port 23) when debug mode is enabled - mirrors all logs over WiFi

## Hardware

### Option 1: MatrixPortal ESP32-S3 (Recommended)

- **Controller**: Adafruit MatrixPortal ESP32-S3
- **Display**: 2× HUB75 64×32 LED Matrix panels (128×32 total resolution)
- **Power Supply**: Minimum 5V 3A switching power supply
- **Features**: Full web UI, captive portal, OTA updates, Prague + Berlin support

### Option 2: Matrix Portal M4

- **Controller**: Adafruit Matrix Portal M4 (SAMD51 + ESP32 WiFi coprocessor)
- **Display**: 2× HUB75 64×32 LED Matrix panels (128×32 total resolution)
- **Power Supply**: Minimum 5V 3A switching power supply
- **Limitations**:
  - Prague/Golemio API only (no Berlin support)
  - No web configuration UI - credentials hardcoded in source
  - No GitHub OTA updates (manual .bin upload only)
  - No captive portal for WiFi setup

**M4 Configuration**: Create `src/config/credentials.h` from the template and edit before building:
```cpp
#define DEFAULT_WIFI_SSID "YourNetwork"
#define DEFAULT_WIFI_PASSWORD "YourPassword"
#define DEFAULT_GOLEMIO_API_KEY "your-api-key"
#define DEFAULT_PRAGUE_STOP_IDS "U899Z3P,U899Z4P"

// Weather location (Open-Meteo API - no key required)
#define DEFAULT_WEATHER_LATITUDE 50.0755f   // Your latitude
#define DEFAULT_WEATHER_LONGITUDE 14.4378f  // Your longitude
```
Weather is enabled by default on M4. Set your GPS coordinates to show local weather.

### Option 3: Generic ESP32-S3

SpojBoard firmware can also run on generic ESP32-S3 development boards with custom wiring. See [Generic ESP32-S3 Guide](docs/GENERIC_ESP32S3.md) for details.

**Caveats:**
- Requires manual GPIO pin mapping configuration
- Needs custom wiring (no plug-and-play connector)
- May require level shifters for some displays
- Cost savings: ~$10-20 per unit

### Power Supply Requirements

HUB75 LED matrix panels require external 5V power.

**Typical power consumption (displaying text):**
- Two 64×32 panels (128×32): ~1-2A at typical brightness showing colored text
- ESP32-S3: ~500mA peak during WiFi transmission
- **Total**: ~2-3A typical

**Note:** Peak consumption with all LEDs at full white would be ~5A, but this never occurs during normal operation with text display.

## Quick Start

### ESP32-S3: First Time Setup (AP Mode)

1. **Build & Flash the Firmware**
   ```bash
   pio run -e adafruit_matrixportal_esp32s3 -t upload
   ```

2. **Connect to Setup AP**
   - The display will show WiFi credentials:
     ```
     WiFi Setup Mode
     SSID: SpojBoard-XXXX
     Pass: xxxxxxxx
     Go to: 192.168.4.1
     ```
   - Connect your phone/computer to this WiFi network
   - A captive portal should open automatically (or go to http://192.168.4.1)

3. **Configure via Web UI**
   - Enter your home WiFi credentials (required)
   - Select your transit city (Prague or Berlin)
   - For Prague: Enter Golemio API key (get one at [api.golemio.cz/api-keys](https://api.golemio.cz/api-keys/))
   - For Berlin: No API key required
   - Enter your stop ID(s) - comma-separated for multiple stops
   - Try the demo mode to preview the display before API setup
   - Click "Save & Connect to WiFi"

4. **Device Connects**
   - The device will restart and connect to your WiFi
   - Find its new IP address on the display or your router
   - Departures will start showing automatically

### If WiFi Fails (ESP32-S3)

If the device can't connect to WiFi (wrong password, network down, etc.), it will automatically:
1. Stop trying after 20 attempts (~10 seconds)
2. Create a new AP with a **random password**
3. Display the credentials on the LED matrix
4. Wait for you to configure new WiFi settings

### Matrix Portal M4: Setup

1. **Edit Configuration** in `src/config/AppConfig.h`:
   ```cpp
   #define DEFAULT_WIFI_SSID "YourNetwork"
   #define DEFAULT_WIFI_PASSWORD "YourPassword"
   #define DEFAULT_GOLEMIO_API_KEY "your-golemio-api-key"
   ```

2. **Set Stop ID** in `src/config/AppConfig.cpp`:
   ```cpp
   strlcpy(config.pragueStopIds, "U899Z3P", sizeof(config.pragueStopIds));
   ```

3. **Build & Flash**:
   ```bash
   pio run -e adafruit_matrix_portal_m4 -t upload
   ```

4. **Monitor** (optional):
   ```bash
   pio device monitor
   ```

The device will connect to WiFi and start showing departures. To change configuration, edit the source files and re-flash.

## Web Interface (ESP32-S3 Only)

The built-in web server provides:

- **Status Dashboard**: WiFi status, API status, memory usage
- **Configuration Form**:
  - WiFi credentials
  - Transit city selector (Prague or Berlin)
  - API key (Prague only)
  - Stop ID(s) - comma-separated for multiple stops (max 12)
  - Refresh interval (10-300 seconds)
  - Number of departures to display (1-3 rows)
  - Minimum departure time (0-30 minutes) - filter out departures too soon to catch
  - Display brightness (0-255)
  - Custom line colors with position-based wildcard patterns (e.g., "9*" for 2-digit, "4**" for 3-digit)
  - Weather display settings (enable/disable, GPS coordinates, refresh interval)
  - Debug mode (enables telnet logging on port 23)
- **Actions**:
  - Force refresh
  - Display demo (preview custom departures)
  - Update firmware (manual .bin upload)
  - Check for updates (GitHub releases)
  - Reboot device
  - Factory reset

### AP Mode vs Normal Mode

| Feature | AP Mode | Normal Mode |
|---------|---------|-------------|
| WiFi | Creates its own network | Connects to your network |
| API Calls | Disabled | Every 30-300s (configurable) |
| ETA Updates | N/A | Every 10s from cache |
| Display | Shows AP credentials | Shows departures |
| Web UI | Setup focused | Full dashboard |
| Demo Mode | Available | Available |

## Getting Your Stop ID

### Prague (Golemio API)
1. Visit [data.pid.cz/stops/json/stops.json](https://data.pid.cz/stops/json/stops.json)
2. Search for your stop name (Ctrl+F)
3. Find the `gtfsIds` value (e.g., `U693Z2P`)
4. You can enter multiple IDs separated by commas

### Berlin (BVG API)
1. Visit [v6.bvg.transport.rest](https://v6.bvg.transport.rest/)
2. Use the `/locations` endpoint to search for your stop
3. Find the stop ID (numeric, e.g., `900013102`)
4. You can enter multiple IDs separated by commas

## Display Layout

### Normal Mode
```
┌────────────────────────────────────┐
│ [31] Sídliště Řepy           12' │  Row 1
│ [7 ] Radlická                 5' │  Row 2
│ [A ] Depo Hostivař            2' │  Row 3
│ Mon  ☀ -3°               14:35   │  Row 4 (Date/Weather/Time)
└────────────────────────────────────┘
```

**Visual Design**:
- Line numbers displayed in their route color on uniform 18-pixel black background boxes (fits 1-3 characters)
- Route numbers are horizontally centered within their boxes for consistent alignment
- All destinations start at the same X position regardless of route length
- Adaptive font rendering: automatically switches to condensed font for destinations longer than 16 characters
- Destination text dynamically truncated based on ETA display width and font choice to prevent overlap

### AP Setup Mode
```
┌────────────────────────────────────┐
│ WiFi Setup Mode                   │
│ SSID: SpojBoard-A1B2       │
│ Pass: k7m3p9x2                    │
│ Go to: 192.168.4.1                │
└────────────────────────────────────┘
```

## Color Coding

### Line Numbers (Default Colors)

These colors are used by default and can be customized via the web interface:

- **Green**: Metro A
- **Yellow**: Metro B, Default fallback for all unmapped lines
- **Red**: Metro C
- **White**: Trams 1-29 (e.g., 2, 5, 7, 10, 12, 15, 17, 22)
- **Purple**: 2-digit T-buses 50-59 & 3-digit buses 100-299 (e.g., 59, 100, 102, 200)
- **Blue**: S-trains (S1, S2, S3, etc.)
- **Cyan**: Night lines 91-99, 900-999

**Customizing Line Colors:**
- Configure specific colors for individual lines (e.g., "A=RED")
- Use position-based wildcard patterns with asterisks as position placeholders:
  - "9*" matches 2-digit lines (91-99)
  - "95*" matches 3-digit lines (950-959)
  - "4**" matches 3-digit lines (400-499)
  - "C***" matches 4-digit lines (C000-C999)
- Exact matches take priority over patterns
- Invalid patterns (leading asterisks "***", non-trailing asterisks "9*1") are ignored
- Unmapped lines use the hardcoded defaults above
- Available colors: RED, GREEN, BLUE, YELLOW, ORANGE, PURPLE, CYAN, WHITE

### ETA Colors
- **White**: > 5 minutes
- **Yellow**: 2-5 minutes
- **Red**: < 2 minutes
- **Orange**: Delayed

### Weather Colors
**Icons** (based on conditions):
- **Yellow**: Clear sky (sun)
- **White**: Cloudy/partly cloudy
- **Purple**: Fog
- **Cyan**: Rain/drizzle
- **Blue**: Snow
- **Red**: Thunderstorm

**Temperature** (based on value):
- **Red**: > 25°C (hot)
- **Yellow**: 17-25°C (warm)
- **White**: 8-16°C (mild)
- **Blue**: < 8°C (cold)

## Troubleshooting

Having issues? See the **[Troubleshooting Guide](docs/TROUBLESHOOTING.md)** for solutions to common problems including WiFi connection issues, API errors, display problems, and firmware update issues.

## Firmware Updates

SpojBoard supports two methods for updating firmware:

### Method 1: GitHub Updates (Recommended)

Check for and install new releases directly from GitHub:

1. **Open the web interface** at `http://[device-ip]/`
2. **Click "Check for Updates"** in the Actions section
3. **Review the update** (version, release notes, file size)
4. **Click "Download & Install"** if update is available
5. **Wait for download** - progress shown on LED matrix
6. **Device reboots automatically** with new firmware

**Features:**
- ✅ Automatic version checking
- ✅ Displays release notes before installing
- ✅ Secure HTTPS download with MD5 validation
- ✅ Progress display on LED matrix
- ✅ Safe - failed update doesn't brick the device

**Requirements:**
- Device must be connected to WiFi (not in AP mode)
- Internet access to github.com

### Method 2: Manual Upload

Upload a firmware .bin file manually:

1. **Get the firmware file** (e.g., `spojboard-r2-abc12345.bin`)
2. **Open web interface** and click "Update Firmware"
3. **Select .bin file** and click "Upload Firmware"
4. **Wait for upload** - progress shown on screen
5. **Device reboots** after successful upload

**Use cases:**
- Installing custom firmware builds
- Offline updates
- Development testing

### Update Security

All firmware updates (both methods):
- ❌ **Disabled in AP mode** (prevents unauthorized access)
- ✅ **MD5 validation** (corrupted firmware rejected)
- ✅ **HTTPS only** for GitHub downloads
- ✅ **Separate OTA partition** (failed update doesn't affect running firmware)
- ✅ **User confirmation required** (no automatic updates)



## Technical Details

For detailed information about the data flow pipeline, memory allocation, and design decisions, see **[Architecture & Data Flow](docs/ARCHITECTURE.md)**.

### Platform Comparison

| Feature | ESP32-S3 | Matrix Portal M4 |
|---------|----------|------------------|
| MCU | ESP32-S3 (native WiFi) | SAMD51 + ESP32 coprocessor |
| Display Library | ESP32-HUB75-MatrixPanel-I2S-DMA | Adafruit Protomatter |
| WiFi Library | Native ESP32 WiFi | WiFiNINA |
| HTTP Client | HTTPClient | ArduinoHttpClient |
| Storage | NVS Preferences | FlashStorage_SAMD |
| Web Server | WebServer | ❌ Not available |
| Captive Portal | ✅ DNSServer | ❌ Not available |
| Prague API | ✅ Golemio | ✅ Golemio |
| Berlin API | ✅ BVG | ❌ Excluded |
| Weather API | ✅ Open-Meteo (HTTPS) | ✅ Open-Meteo (HTTP) |
| GitHub OTA | ✅ Full support | ❌ Not available |
| Telnet Logging | ✅ ESPTelnet | ❌ Not available |
| RAM Usage | ~70KB / 327KB (21%) | ~17KB / 192KB (9%) |
| Flash Usage | ~1.2MB / 1.3MB (95%) | ~117KB / 508KB (23%) |

### AP Mode Credentials (ESP32-S3 Only)
- SSID: `SpojBoard-XXXX` (XXXX = last 4 chars of MAC address)
- Password: 8 random alphanumeric characters (regenerated each time)
- IP: `192.168.4.1`

### Captive Portal
The device implements captive portal detection for:
- Android (`/generate_204`, `/gen_204`)
- iOS/macOS (`/hotspot-detect.html`)
- Windows (`/ncsi.txt`, `/connecttest.txt`)
- Firefox (`/success.txt`)

### Memory Usage
- JSON buffer: 8KB for API responses
- Configuration stored in NVS flash
- Typical free heap: ~200KB
- RAM usage: 21.4% (70KB used of 327KB)
- Flash usage: 94.7% (1.24MB used of 1.31MB)

### Multi-Stop Behavior
When multiple stop IDs are configured (comma-separated, max 12 stops), the system:
1. Queries each stop individually via separate API calls (always fetches 12 departures per stop)
2. Applies 1-second delay between API calls to reduce server load and avoid rate limiting
3. Collects all departures in temporary buffer (capacity: 144 total = 12 stops × 12 departures)
4. Sorts by ETA (earliest departures first across all stops)
5. Caches the top 12 soonest departures with timestamps for ETA recalculation
6. Displays the configured number of rows (1-3) on the LED matrix
7. Recalculates ETAs every 10 seconds from cached timestamps without additional API calls

This ensures you always see the **soonest** departures across all your stops, regardless of which stop they come from. The API always fetches 12 departures per stop for caching and sorting - the user only controls how many rows (1-3) to display on the LED matrix. The 10-second ETA recalculation keeps the display fresh without hammering the API, allowing longer refresh intervals (up to 300s) during peak times.

### Destination String Shortening
To maximize display space on the 128×32 LED matrix, long Czech destination names are automatically shortened before display (Prague only):
- "Nádraží" → "Nádr." (uppercase)
- "nádraží" → "nádr." (lowercase)
- "Sídliště" → "Sídl."
- "Nemocnice" → "Nem."

Combined with adaptive font rendering (condensed font for destinations >16 chars), even very long station names fit comfortably on the display. Shortening is applied before UTF-8 to ISO-8859-2 conversion, ensuring Czech diacritics are preserved correctly. Berlin station names are displayed as-is from the API.

## Fonts & Character Support

SpojBoard uses custom 8-bit ISO-8859-2 fonts with automatic UTF-8 conversion to display special characters (**Czech**: ž, š, č, ř, ň, ť, ď, ú, ů, á, é, í, ó, ý; **German**: ß, ẞ, ä, ö, ü, Ä, Ö, Ü) from transit APIs.

For complete details on the font system, UTF-8 conversion, and creating custom fonts, see **[Font System Documentation](docs/FONTS.md)**.

## License

This project is licensed under the **GNU General Public License v3.0 (GPL-3.0)**.

This means you are free to:
- ✅ Use this code for personal or commercial purposes
- ✅ Modify and distribute the code
- ✅ Create derivative works

**However, you must**:
- 📝 Disclose the source code of any derivative works
- 📝 License derivative works under GPL-3.0
- 📝 Include copyright and license notices

See the LICENSE file for full details.

## Credits

- [Golemio API](https://api.golemio.cz) - Prague open data platform
- [Open-Meteo](https://open-meteo.com) - Free weather API (no key required)
- [ESP32-HUB75-MatrixPanel-DMA](https://github.com/mrfaptastic/ESP32-HUB75-MatrixPanel-DMA) - Display driver
- [Adafruit GFX Library](https://github.com/adafruit/Adafruit-GFX-Library) - Graphics primitives and font rendering
- [Adafruit GFX Font Customiser by tchapi](https://tchapi.github.io/Adafruit-GFX-Font-Customiser/)
- [Michel Deslierres](https://sigmdel.ca/michel/program/misc/gfxfont_8bit_en.html) - Original UTF-8 to ISO-8859-2 conversion code
- [Petr Brouzda - fontconvert8-iso8859-2](https://github.com/petrbrouzda/fontconvert8-iso8859-2) - Font conversion tool and UTF-8 conversion implementation
- [Claude Code](https://claude.ai/code) - AI-assisted development and code refinement
- [DepartureMono font family](https://departuremono.com/) - Custom 8-bit GFXfonts with ISO-8859-2 character support