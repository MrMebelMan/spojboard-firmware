# SpojBoard - Smart Panel for Onward Journeys

An ESP32-based LED matrix display showing real-time transit departures from Prague's Golemio API.

**SPOJ** = **S**mart **P**anel for **O**nward **J**ourneys (also "spoj" = connection/service in Czech)

No external server required - standalone operation with web-based configuration!

![SpojBoard Device](docs/images/spojboard-device.jpg)

**[📸 View More Screenshots](docs/SCREENSHOTS.md)**

## Documentation

- **[Architecture & Data Flow](docs/ARCHITECTURE.md)** - Technical details, pipeline flow, and design rationale
- **[Font System](docs/FONTS.md)** - Czech character support and custom font creation guide
- **[Hardware Guide for Generic ESP32-S3](docs/GENERIC_ESP32S3.md)** - Alternative hardware setup

## Features

- **Direct API Access**: Fetches departure data from Golemio PID API every 10-300 seconds (configurable)
- **Smart ETA Updates**: Recalculates ETAs every 10 seconds from cached timestamps without additional API calls - reduces server load while keeping display fresh
- **Extended Buffer**: Always fetches and caches 12 departures per stop for optimal sorting and filtering
- **Multi-Stop Support**: Query up to 12 stops simultaneously (144 departure buffer), automatically sorted by departure time with 1s rate limiting
- **WiFi Setup Portal**: Automatic AP mode with captive portal when WiFi fails - no code editing needed!
- **Web Configuration**: Easy setup via built-in web interface
- **Persistent Settings**: Configuration stored in ESP32 flash memory, survives reboots
- **Full Czech Character Support**: Custom 8-bit ISO-8859-2 fonts with automatic UTF-8 conversion for perfect display of Czech station names (ž, š, č, ř, ň, ť, ď, etc.)
- **Adaptive Font Rendering**: Automatically switches to condensed font for long destination names (>16 chars), fitting up to 23 characters on screen
- **Smart Text Shortening**: Automatic shortening of long Czech words (e.g., "Nádraží" → "Nádr.", "nádraží" → "nádr.") to maximize display space
- **Smart Filtering**: Configurable minimum departure time (default 3 minutes) to hide departures that are too soon to catch
- **Real-time Updates**: Shows line number, destination, and ETA in minutes
- **AC Indicator**: Shows asterisk (*) for air-conditioned vehicles
- **Color-coded Lines**: Colored line numbers on black backgrounds for high contrast and readability
- **Custom Line Colors**: Configure custom colors for specific lines via web interface (supports pattern matching with wildcards)
- **Firmware Updates**: GitHub-based OTA updates with user confirmation - check for new releases directly from the web interface
- **Optional Telnet Logging**: Remote debugging via telnet (port 23) when debug mode is enabled - mirrors all logs over WiFi

## Hardware

### Recommended Setup

- **Controller**: Adafruit MatrixPortal ESP32-S3
- **Display**: 2× HUB75 64×32 LED Matrix panels (128×32 total resolution)
- **Power Supply**: Minimum 5V 3A switching power supply
  - Can also be powered by a 5V 3A USB-C charger: wire the power cables from the displays to MatrixPortal's 5V/GND terminals and power through its USB-C port.

### Alternative: Generic ESP32-S3

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

### First Time Setup (AP Mode)

1. **Build & Flash the Firmware**
   ```bash
   pio run -t upload
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
   - Enter your home WiFi credentials
   - Enter your Golemio API key (get one at [api.golemio.cz/api-keys](https://api.golemio.cz/api-keys/))
   - Enter your stop ID(s)
   - Click "Save & Connect to WiFi"

4. **Device Connects**
   - The device will restart and connect to your WiFi
   - Find its new IP address on the display or your router
   - Departures will start showing automatically

### If WiFi Fails

If the device can't connect to WiFi (wrong password, network down, etc.), it will automatically:
1. Stop trying after 20 attempts (~10 seconds)
2. Create a new AP with a **random password**
3. Display the credentials on the LED matrix
4. Wait for you to configure new WiFi settings

This ensures you can always access the device for reconfiguration!

## Web Interface

The built-in web server provides:

- **Status Dashboard**: WiFi status, API status, memory usage
- **Configuration Form**:
  - WiFi credentials
  - Golemio API key
  - Stop ID(s) - comma-separated for multiple stops (max 12)
  - Refresh interval (10-300 seconds)
  - Number of departures to display (1-3 rows)
  - Minimum departure time (0-30 minutes) - filter out departures too soon to catch
  - Display brightness (0-255)
  - Custom line colors with pattern matching (e.g., "9*/9**" for all night lines)
  - Debug mode (enables telnet logging on port 23)
- **Actions**:
  - Force refresh
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

## Getting Your Stop ID

1. Visit [data.pid.cz/stops/json/stops.json](https://data.pid.cz/stops/json/stops.json)
2. Search for your stop name (Ctrl+F)
3. Find the `gtfsIds` value (e.g., `U693Z2P`)
4. You can enter multiple IDs separated by commas

## Display Layout

### Normal Mode
```
┌────────────────────────────────────┐
│ [31] Sídliště Řepy           12' │  Row 1
│ [7 ] Radlická                 5' │  Row 2
│ [A ] Depo Hostivař            2' │  Row 3
│ Mon| Feb 15              14:35    │  Row 4 (Date/Time)
└────────────────────────────────────┘
```

**Visual Design**:
- Line numbers displayed in their route color on uniform 18-pixel black background boxes (fits 1-3 characters)
- Route numbers are horizontally centered within their boxes for consistent alignment
- All destinations start at the same X position regardless of route length
- Adaptive font rendering: automatically switches to condensed font for destinations longer than 16 characters
- Destination text dynamically truncated based on ETA display width and font choice to prevent overlap
- High contrast design optimized for LED matrix visibility

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
- **Purple**: 3-digit trams 100-299 (e.g., 100, 102, 200)
- **Blue**: S-trains (S1, S2, S3, etc.)
- **Cyan**: Night lines 91-99, 900-999

**Customizing Line Colors:**
- Configure specific colors for individual lines (e.g., "A=RED")
- Use pattern matching with trailing asterisk (e.g., "9*=CYAN" matches all lines starting with 9)
- Exact matches take priority over patterns
- Unmapped lines use the hardcoded defaults above
- Available colors: RED, GREEN, BLUE, YELLOW, ORANGE, PURPLE, CYAN, WHITE

### ETA Colors
- **White**: > 5 minutes
- **Yellow**: 2-5 minutes
- **Red**: < 2 minutes
- **Orange**: Delayed

## Troubleshooting

### Can't Connect to AP
- Make sure you're connecting to the right network (SpojBoard-XXXX)
- The password is case-sensitive and shown on the display
- Try forgetting the network and reconnecting

### "API Error: HTTP 401"
- Invalid API key - check your key at api.golemio.cz

### "API Error: HTTP 429"
- Rate limited - increase refresh interval

### "No Departures"
- Check stop ID is correct
- Some stops may have no service at certain times

### Device Keeps Going to AP Mode
- Double-check your WiFi password
- Make sure your WiFi is 2.4GHz (ESP32-S3 doesn't support 5GHz)
- Check if your router is blocking new devices

### Display Issues
- Check HUB75 cable connections
- Verify panel chain order

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

### Troubleshooting Updates

**"Updates not available in AP mode"**
- Connect device to your WiFi network first
- AP mode blocks updates for security

**"No update available"**
- Your device is already running the latest version
- Check [GitHub Releases](https://github.com/xbach/spojboard-firmware/releases) to verify

**"Download failed" or "Installation failed"**
- Check internet connection
- Try again - download may have been interrupted
- Verify you have enough space (firmware ~1-2 MB)

**"GitHub API error" or "Rate limit exceeded"**
- GitHub limits: 60 requests per hour for unauthenticated users
- Wait an hour and try again
- This is normal and not a device issue

## Architecture

The codebase follows a modular, layered architecture with zero circular dependencies:

```
src/
├── main.cpp                          # Application orchestration (340 lines)
├── config/
│   ├── AppConfig.h/cpp              # Configuration structure & NVS persistence
├── display/
│   ├── DisplayManager.h/cpp         # Display rendering & layout
│   ├── DisplayColors.h/cpp          # Color system & line color mapping
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

### Design Principles

**Layered Dependencies** (lower layers never depend on higher layers):
1. **Foundation Layer**: Logger, UTF-8 utilities
2. **Data Layer**: AppConfig, DepartureData
3. **Hardware Layer**: TimeUtils, DisplayColors, DisplayManager
4. **Network Layer**: CaptivePortal, WiFiManager, ConfigWebServer
5. **Business Logic**: GolemioAPI
6. **Application**: main.cpp (orchestrates all modules)

**Key Patterns**:
- **Zero Circular Dependencies**: Strict unidirectional dependency flow
- **Single Responsibility**: Each module has one clear purpose
- **Callback Pattern**: Modules communicate upward via callbacks (e.g., ConfigWebServer → main.cpp)
- **Pure Data Structures**: Config passed as parameter, not stored in modules
- **Static Allocation**: No dynamic allocation in main loop for stability

## Technical Details

For detailed information about the data flow pipeline, memory allocation, and design decisions, see **[Architecture & Data Flow](docs/ARCHITECTURE.md)**.

### AP Mode Credentials
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

This ensures you always see the **soonest** departures across all your stops, regardless of which stop they come from. The API always fetches 12 departures per stop for optimal caching and sorting - the user only controls how many rows (1-3) to display on the LED matrix. The 10-second ETA recalculation keeps the display fresh without hammering the API, allowing longer refresh intervals (up to 300s) during peak times.

### Destination String Shortening
To maximize display space on the 128×32 LED matrix, long Czech destination names are automatically shortened before display:
- "Nádraží" → "Nádr." (uppercase)
- "nádraží" → "nádr." (lowercase)
- "Sídliště" → "Sídl."
- "Nemocnice" → "Nem."

Combined with adaptive font rendering (condensed font for destinations >16 chars), even very long station names fit comfortably on the display. Shortening is applied before UTF-8 to ISO-8859-2 conversion, ensuring Czech diacritics are preserved correctly.

## Fonts & Czech Character Support

SpojBoard uses custom 8-bit ISO-8859-2 fonts with automatic UTF-8 conversion to perfectly display Czech diacritics (**ž, š, č, ř, ň, ť, ď, ú, ů, á, é, í, ó, ý**) from the Golemio API.

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
- [ESP32-HUB75-MatrixPanel-DMA](https://github.com/mrfaptastic/ESP32-HUB75-MatrixPanel-DMA) - Display driver
- [Adafruit GFX Library](https://github.com/adafruit/Adafruit-GFX-Library) - Graphics primitives and font rendering
- [Adafruit GFX Font Customiser by tchapi](https://tchapi.github.io/Adafruit-GFX-Font-Customiser/)
- [Michel Deslierres](https://sigmdel.ca/michel/program/misc/gfxfont_8bit_en.html) - Original UTF-8 to ISO-8859-2 conversion code
- [Petr Brouzda - fontconvert8-iso8859-2](https://github.com/petrbrouzda/fontconvert8-iso8859-2) - Font conversion tool and UTF-8 conversion implementation
- [Claude Code](https://claude.ai/code) - AI-assisted development and code refinement
- [DepartureMono font family](https://departuremono.com/) - Custom 8-bit GFXfonts with ISO-8859-2 character support