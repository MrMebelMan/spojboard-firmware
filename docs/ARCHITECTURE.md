# SpojBoard Architecture & Data Flow

Technical documentation for developers and contributors.

## Table of Contents

- [Configuration Constants](#configuration-constants)
- [Complete Pipeline Flow](#complete-pipeline-flow)
- [Design Rationale](#design-rationale)
- [Memory Allocation](#memory-allocation)
- [State Machine](#state-machine)
- [Module Dependencies](#module-dependencies)

## Configuration Constants

- **`MAX_DEPARTURES = 12`** ([DepartureData.h:10](../src/api/DepartureData.h#L10)) - Maximum cache size (hardcoded)
- **`MAX_TEMP_DEPARTURES = 144`** ([GolemioAPI.h:51](../src/api/GolemioAPI.h#L51)) - Collection buffer size (12 stops × 12 departures)
- **`config.numDepartures`** - User setting for display rows (1-3 only)

**Important:** `config.numDepartures` only controls how many rows to show on the LED matrix (1-3), not API fetch size. The API always fetches `MAX_DEPARTURES` (12) per stop for better caching and sorting. This simplifies the user experience - users don't need to understand API response lengths.

## Complete Pipeline Flow

```
┌──────────────────────────────────────────────────────────────────┐
│ 1. USER CONFIGURATION                                            │
│    config.numDepartures = 2        # Show 2 rows on display      │
│    config.stopIds = "A,B"          # Query 2 stops               │
└──────────────────────────────────────────────────────────────────┘
                              ↓
┌──────────────────────────────────────────────────────────────────┐
│ 2. API QUERIES (Always fetch MAX_DEPARTURES = 12 per stop)      │
│    GolemioAPI::fetchDepartures() loops through stops:            │
│    - Stop A: API call → 12 departures → tempDepartures[0-11]    │
│    - delay(1000)  # 1-second rate limiting                       │
│    - Stop B: API call → 12 departures → tempDepartures[12-23]   │
│    Total collected: 24 departures in temporary buffer            │
│    Buffer capacity: 144 (supports up to 12 stops)                │
└──────────────────────────────────────────────────────────────────┘
                              ↓
┌──────────────────────────────────────────────────────────────────┐
│ 3. SORT BY ETA (GolemioAPI.cpp:71)                              │
│    qsort(tempDepartures, 24, ..., compareDepartures)            │
│    All departures sorted by increasing ETA across all stops      │
│    Example sorted result:                                        │
│    [0] = Stop B, Line 7, ETA 2min                                │
│    [1] = Stop A, Line 31, ETA 5min                               │
│    [2] = Stop B, Line A, ETA 8min                                │
│    ... (21 more)                                                 │
└──────────────────────────────────────────────────────────────────┘
                              ↓
┌──────────────────────────────────────────────────────────────────┐
│ 4. COPY TO CACHE (GolemioAPI.cpp:81-88)                         │
│    Copy top MAX_DEPARTURES (12) from sorted temp to cache:       │
│    for (i = 0; i < tempCount && count < MAX_DEPARTURES; i++)    │
│        result.departures[count++] = tempDepartures[i];           │
│    Result:                                                       │
│    - result.departures[12] = top 12 soonest departures           │
│    - result.departureCount = 12                                  │
│    - Each departure includes departureTime (Unix timestamp)      │
└──────────────────────────────────────────────────────────────────┘
                              ↓
┌──────────────────────────────────────────────────────────────────┐
│ 5. MAIN LOOP STORAGE (main.cpp)                                 │
│    Global cache in main.cpp:                                     │
│    - Departure departures[MAX_DEPARTURES] = cached results       │
│    - int departureCount = number of valid departures             │
│    Cache persists between API calls for ETA recalculation        │
└──────────────────────────────────────────────────────────────────┘
                              ↓
┌──────────────────────────────────────────────────────────────────┐
│ 6. REAL-TIME ETA UPDATES (Every 10 seconds, main.cpp)           │
│    recalculateETAs():                                            │
│    - For each departure in cache:                                │
│        eta = calculateETA(departure.departureTime)               │
│    - Remove stale departures (ETA < 0 or invalid)                │
│    - No API call needed - uses cached timestamps                 │
│    This keeps display fresh without hammering the API!           │
└──────────────────────────────────────────────────────────────────┘
                              ↓
┌──────────────────────────────────────────────────────────────────┐
│ 7. DISPLAY RENDERING (DisplayManager.cpp:345-414)               │
│    updateDisplay(..., departures, departureCount, numToDisplay)  │
│    - rowsToDraw = min(departureCount, numToDisplay, 3)          │
│    - rowsToDraw = min(12, 2, 3) = 2                             │
│    - for (i = 0; i < 2; i++): drawDeparture(i, departures[i])   │
│    Only first 2 departures shown on LED matrix (user setting)    │
│    Physical maximum is 3 rows (128×32 display = 4 rows total,   │
│    with row 4 reserved for date/time status bar)                │
└──────────────────────────────────────────────────────────────────┘
```

## Design Rationale

### 1. Always Fetch MAX_DEPARTURES (12)
- Ensures good caching regardless of display setting
- Simplifies API logic - no user-dependent behavior
- Better sorting with more data points
- Users don't need to understand API response sizes

### 2. Large Temp Buffer (144)
- Supports up to 12 stops × 12 departures = 144 total
- Prevents data loss when querying multiple stops
- Memory cost: ~7KB (acceptable on ESP32 with ~200KB free)

### 3. Fixed Cache Size (12)
- Keeps "best" 12 departures after sorting
- Reasonable memory usage (~600 bytes)
- More departures than can be displayed (3) for filtering flexibility

### 4. Display-Only User Control (1-3)
- Maps directly to physical LED matrix rows
- Simple to understand: "How many rows to show?"
- No technical knowledge required

### 5. 10-Second ETA Recalculation
- Keeps display fresh without API calls
- Allows longer refresh intervals (up to 300s) to reduce load
- Filters out stale departures automatically

## Memory Allocation

### Data Structures

- **Temp Buffer**: `static Departure tempDepartures[144]` (~7KB)
  - Function-local static to avoid stack overflow
  - Located in `GolemioAPI::fetchDepartures()`
  - Allocated once at compile time

- **Cache**: `Departure departures[12]` (~600 bytes)
  - Global in `main.cpp`
  - Persists between API calls
  - Used for ETA recalculation

- **Display**: No departure storage
  - Receives pointer to cache
  - Zero memory overhead

**Total**: ~8KB for departure data structures

### Heap Usage

- JSON buffer: 8KB for API responses (DynamicJsonDocument)
- Configuration: NVS flash storage (persistent across reboots)
- Typical free heap: ~200KB
- RAM usage: 21.4% (70KB used of 327KB)
- Flash usage: 94.7% (1.24MB used of 1.31MB)

## State Machine

The device operates in two modes:

### AP Mode (`apModeActive=true`)
- Creates WiFi network for setup
- DNS captive portal active
- Display shows credentials (SSID/password/IP)
- API calls disabled
- Web UI shows setup-focused interface

### STA Mode (`apModeActive=false`)
- Connects to configured WiFi
- Fetches departures every N seconds (configurable)
- ETA recalculation every 10 seconds
- Serves full web dashboard
- Demo mode available

### Demo Mode (`demoModeActive=true`)
- Pauses API polling and automatic display updates
- Shows user-configurable sample departures
- Available in both AP and STA modes
- Manually stopped via web interface or device reboot

### State Transitions

```
Boot
  ↓
Try STA mode (20 attempts, ~10s)
  ↓
  ├─ Success → STA Mode
  │             ↓
  │           Connection loss?
  │             ↓
  │           Auto-reconnect (every 30s)
  │
  └─ Failure → AP Mode
               ↓
             Config saved?
               ↓
             Restart → Try STA mode
```

## Module Dependencies

Layered architecture with zero circular dependencies:

```
┌─────────────────────────────────────────────────────────┐
│ Layer 6: Application                                    │
│   main.cpp (orchestrates all modules)                   │
└─────────────────────────────────────────────────────────┘
                        ↓
┌─────────────────────────────────────────────────────────┐
│ Layer 5: Business Logic                                 │
│   GolemioAPI, GitHubOTA                                 │
└─────────────────────────────────────────────────────────┘
                        ↓
┌─────────────────────────────────────────────────────────┐
│ Layer 4: Network Services                               │
│   WiFiManager, CaptivePortal, ConfigWebServer           │
│   OTAUpdateManager                                       │
└─────────────────────────────────────────────────────────┘
                        ↓
┌─────────────────────────────────────────────────────────┐
│ Layer 3: Hardware Abstraction                           │
│   DisplayManager, DisplayColors, TimeUtils              │
└─────────────────────────────────────────────────────────┘
                        ↓
┌─────────────────────────────────────────────────────────┐
│ Layer 2: Data Layer                                     │
│   AppConfig, DepartureData                              │
└─────────────────────────────────────────────────────────┘
                        ↓
┌─────────────────────────────────────────────────────────┐
│ Layer 1: Foundation                                     │
│   Logger, UTF-8 utilities (gfxlatin2, decodeutf8)       │
└─────────────────────────────────────────────────────────┘
```

### Key Patterns

- **Zero Circular Dependencies**: Lower layers never depend on higher layers
- **Single Responsibility**: Each module has one clear purpose
- **Callback Pattern**: Modules communicate upward via callbacks
  - Example: `ConfigWebServer` → `main.cpp` via `onSaveConfig` callback
- **Pure Data Structures**: Config passed as parameter, not stored in modules
- **Static Allocation**: No dynamic allocation in main loop for stability

### Module Communication

```
main.cpp
  ├─ Creates all modules
  ├─ Registers callbacks
  ├─ Owns global state (departures array)
  └─ Orchestrates timing (API calls, ETA updates, display refresh)

WiFiManager
  ├─ Manages WiFi connection
  ├─ Notifies main.cpp of state changes via flags
  └─ Provides status query methods

GolemioAPI
  ├─ Fetches departures via HTTP
  ├─ Returns APIResult struct (no state stored)
  └─ Uses statusCallback for progress updates

DisplayManager
  ├─ Renders to LED matrix
  ├─ Receives data as parameters (no caching)
  ├─ Handles UTF-8 to ISO-8859-2 conversion at render time
  └─ Accesses config pointer for color mapping

ConfigWebServer
  ├─ Serves web interface
  ├─ Handles demo mode via callbacks
  └─ Communicates with main.cpp via callback pattern
```

## Multi-Stop Behavior

When multiple stop IDs are configured (comma-separated, max 12 stops):

1. **Query each stop individually** via separate API calls (always 12 departures per stop)
2. **Apply 1-second delay** between API calls to reduce server load and avoid rate limiting
3. **Collect in temp buffer** (capacity: 144 = 12 stops × 12 departures)
4. **Sort by ETA** (earliest departures first across all stops)
5. **Cache top 12** soonest departures with timestamps
6. **Display configured rows** (1-3) on LED matrix
7. **Recalculate ETAs** every 10 seconds without additional API calls

This ensures you always see the **soonest** departures across all stops, regardless of which stop they come from.

### Rate Limiting

The 1-second delay between API calls (`delay(1000)` in [GolemioAPI.cpp:63](../src/api/GolemioAPI.cpp#L63)) prevents:
- HTTP 429 (Too Many Requests) errors
- Excessive load on Golemio API servers
- Connection timeouts from rapid requests

With 12 stops configured, a full query cycle takes ~12 seconds (plus network latency).

## Performance Characteristics

### API Call Timing
- **Single stop**: ~1-2 seconds (network latency)
- **Multiple stops**: ~1-2s per stop + 1s delay between stops
- **12 stops**: ~12-24 seconds total

### Display Update Timing
- **ETA recalculation**: <1ms (simple arithmetic on cached data)
- **Display render**: ~10-20ms (LED matrix DMA transfer)
- **Total refresh cycle**: ~30ms

### Memory Footprint
- **Stack usage**: Minimal (all large arrays are static or global)
- **Heap fragmentation**: None (no dynamic allocation in main loop)
- **Flash storage**: Configuration in NVS (~1KB)

## Debugging & Logging

### Debug Mode (Telnet)
When `config.debugMode = true`:
- Telnet server listens on port 23
- All `debugPrintln()` calls mirrored to telnet clients
- Memory usage logged at key points
- API responses logged with timestamps

### Serial Output
Always available (115200 baud):
- Boot sequence
- WiFi connection status
- API errors
- Configuration changes

### Memory Monitoring
Key checkpoints logged via `logMemory()`:
- `api_start` - Before API call
- `api_complete` - After processing response
- `display_update` - After display render

Use telnet to monitor memory in real-time:
```bash
telnet <device-ip> 23
```
