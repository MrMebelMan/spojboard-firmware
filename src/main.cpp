#include <Arduino.h>

// Project modules
#include "utils/Logger.h"
#include "utils/TimeUtils.h"
#include "utils/gfxlatin2.h"
#include "utils/TelnetLogger.h"
#include "config/AppConfig.h"
#include "api/DepartureData.h"
#include "api/GolemioAPI.h"
#if !defined(MATRIX_PORTAL_M4)
#include "api/BvgAPI.h"
#endif
#include "display/DisplayManager.h"
#include "network/WiFiManager.h"
#include "network/CaptivePortal.h"
#include "network/ConfigWebServer.h"

// Platform-specific helpers
#if defined(MATRIX_PORTAL_M4)
    static inline void systemRestart() { NVIC_SystemReset(); }
    static inline uint32_t getFreeHeap() { return 0; }  // Not available on M4
#else
    static inline void systemRestart() { ESP.restart(); }
    static inline uint32_t getFreeHeap() { return ESP.getFreeHeap(); }
#endif

// Hardware configuration and defaults are now in config/AppConfig.h

// ============================================================================
// Global Objects
// ============================================================================
DisplayManager displayManager;
WiFiManager wifiManager;
CaptivePortal captivePortal;
ConfigWebServer webServer;
GolemioAPI golemioAPI;  // Prague transit API
#if !defined(MATRIX_PORTAL_M4)
BvgAPI bvgAPI;          // Berlin transit API
#endif
TransitAPI* transitAPI = nullptr;  // Pointer to active API (selected at runtime)

// ============================================================================
// Configuration Storage (structure defined in config/AppConfig.h)
// ============================================================================
Config config;

// ============================================================================
// Departure Data (structure defined in api/DepartureData.h)
// ============================================================================
Departure departures[MAX_DEPARTURES];
int departureCount = 0;

// ============================================================================
// State Variables
// ============================================================================
unsigned long lastApiCall = 0;
unsigned long lastDisplayUpdate = 0;
unsigned long lastEtaRecalc = 0;  // For 10-second ETA recalculation
bool needsDisplayUpdate = false;
bool apiError = false;
char apiErrorMsg[64] = "";
char stopName[64] = "";
bool demoModeActive = false;  // Demo mode flag - stops API polling and display updates

// Network layer is now in network/ modules:
// - WiFiManager: WiFi connection and AP mode
// - CaptivePortal: DNS server and detection endpoints
// - ConfigWebServer: Web interface handlers

// Forward declarations
bool isCityConfigured();

// ============================================================================
// API Status Callback - Updates display during retries
// ============================================================================
void onAPIStatus(const char* message)
{
    displayManager.drawStatus(message, "", COLOR_YELLOW);
}

// ============================================================================
// Partial Results Callback - Updates display as each stop is queried
// ============================================================================
void onPartialResults(const Departure* partialDepartures, int count, const char* partialStopName)
{
    // Update global state with partial results
    departureCount = count;
    for (int i = 0; i < count; i++)
    {
        departures[i] = partialDepartures[i];
    }
    if (partialStopName && partialStopName[0])
    {
        strlcpy(stopName, partialStopName, sizeof(stopName));
    }

    // Clear any previous error since we have data
    apiError = false;

    // Trigger immediate display update
    displayManager.updateDisplay(departures, departureCount, config.numDepartures,
                                 wifiManager.isConnected(), wifiManager.isAPMode(),
                                 wifiManager.getAPSSID(), wifiManager.getAPPassword(),
                                 apiError, apiErrorMsg,
                                 stopName, isCityConfigured(),
                                 demoModeActive);
}

// ============================================================================
// ETA Recalculation - Updates ETAs from cached timestamps every 10s
// ============================================================================
void recalculateETAs()
{
    // Recalculate ETAs from cached departureTime timestamps
    // Filter out stale departures (past their departure time)
    time_t now = getCurrentEpochTime();

    logTimestamp();
    char startMsg[64];
    snprintf(startMsg, sizeof(startMsg), "ETA Recalc: Processing %d departures (now=%ld)", departureCount, (long)now);
    debugPrintln(startMsg);

    int validCount = 0;
    int filteredCount = 0;
    for (int i = 0; i < departureCount; i++)
    {
        int diffSec = difftime(departures[i].departureTime, now);
        int eta = (diffSec > 0) ? (diffSec / 60) : 0;

        // Log first 3 departures for debugging
        if (i < 3)
        {
            logTimestamp();
            char debugMsg[128];
            snprintf(debugMsg, sizeof(debugMsg), "  [%d] Line %s: depTime=%ld, diffSec=%d, eta=%d min",
                     i, departures[i].line, (long)departures[i].departureTime, diffSec, eta);
            debugPrintln(debugMsg);
        }

        // Only keep departures above minimum departure time
        int minEta = (config.minDepartureTime > 0) ? config.minDepartureTime : 0;
        if (eta > minEta)
        {
            // Copy departure if we're filtering out previous entries
            if (validCount != i)
            {
                departures[validCount] = departures[i];
            }
            departures[validCount].eta = eta;
            validCount++;
        }
        else
        {
            filteredCount++;
            if (filteredCount <= 3)  // Log first 3 filtered departures
            {
                logTimestamp();
                char filterMsg[128];
                snprintf(filterMsg, sizeof(filterMsg), "  Filtered: Line %s (eta=%d min, minEta=%d min, diffSec=%d)",
                         departures[i].line, eta, minEta, diffSec);
                debugPrintln(filterMsg);
            }
        }
    }

    // Update count if we filtered any departures
    if (validCount != departureCount)
    {
        logTimestamp();
        char msg[64];
        snprintf(msg, sizeof(msg), "ETA Recalc: Filtered %d stale departures, %d remain", filteredCount, validCount);
        debugPrintln(msg);
    }

    departureCount = validCount;

    // Resort departures by ETA after recalculation
    if (departureCount > 1)
    {
        logTimestamp();
        debugPrintln("ETA Recalc: Resorting departures by ETA");
        qsort(departures, departureCount, sizeof(Departure), compareDepartures);

        // Log final order (first 3)
        for (int i = 0; i < departureCount && i < 3; i++)
        {
            logTimestamp();
            char sortMsg[96];
            snprintf(sortMsg, sizeof(sortMsg), "  After sort [%d]: Line %s, ETA=%d min",
                     i, departures[i].line, departures[i].eta);
            debugPrintln(sortMsg);
        }
    }

    logTimestamp();
    debugPrintln("ETA Recalc: Complete, display update triggered");
    needsDisplayUpdate = true;
}

// ============================================================================
// Helper Functions
// ============================================================================

// Check if current city has valid API configuration
bool isCityConfigured()
{
    if (!config.configured)
    {
        return false;
    }

    if (strcmp(config.city, "Berlin") == 0)
    {
        // Berlin only needs stop IDs
        return strlen(config.berlinStopIds) > 0;
    }
    else
    {
        // Prague needs both API key and stop IDs
        return strlen(config.pragueApiKey) > 0 && strlen(config.pragueStopIds) > 0;
    }
}

// ============================================================================
// API Fetch Wrapper - Uses TransitAPI interface (GolemioAPI or BvgAPI)
// ============================================================================
void fetchDepartures()
{
    if (!wifiManager.isConnected() || transitAPI == nullptr)
    {
        return;
    }

    // Call API client
    TransitAPI::APIResult result = transitAPI->fetchDepartures(config);

    // Update global state with results
    departureCount = result.departureCount;
    for (int i = 0; i < result.departureCount; i++)
    {
        departures[i] = result.departures[i];
    }

    strlcpy(stopName, result.stopName, sizeof(stopName));

    apiError = result.hasError;
    if (result.hasError)
    {
        strlcpy(apiErrorMsg, result.errorMsg, sizeof(apiErrorMsg));
    }

    needsDisplayUpdate = true;
}

// ============================================================================
// Callback Functions for ConfigWebServer
// ============================================================================

void onConfigSave(const Config &newConfig, bool wifiChanged)
{
    // Update config
    config = newConfig;
    saveConfig(config);

    // Apply brightness immediately
    displayManager.setBrightness(config.brightness);

    if (wifiChanged)
    {
        // Restart to apply new WiFi settings
        delay(1000);
        systemRestart();
    }
    else
    {
        // Trigger immediate API refresh
        lastApiCall = 0;
    }
}

void onRefresh()
{
    lastApiCall = 0; // Force immediate refresh
}

void onReboot()
{
    delay(500);
    systemRestart();
}

void onDemoStart(const Departure* demoDepartures, int demoCount)
{
    // Enter demo mode: stop API polling and display updates
    demoModeActive = true;

    // Copy demo departures to global state
    departureCount = (demoCount > MAX_DEPARTURES) ? MAX_DEPARTURES : demoCount;
    for (int i = 0; i < departureCount; i++)
    {
        departures[i] = demoDepartures[i];
    }

    // Trigger display update with demo data
    needsDisplayUpdate = true;

    logTimestamp();
    debugPrintln("Demo mode activated - API polling stopped");
}

void onDemoStop()
{
    // Exit demo mode: resume normal operation
    demoModeActive = false;
    lastApiCall = 0;  // Force immediate API refresh

    logTimestamp();
    debugPrintln("Demo mode deactivated - resuming normal operation");
}

// ============================================================================
// Setup
// ============================================================================
void setup()
{
    Serial.begin(115200);
    delay(1000);

    // Boot banner always prints to Serial (before logger init)
    Serial.println("\n╔═══════════════════════════════════════╗");
    Serial.println("║          SpojBoard v" FIRMWARE_RELEASE "                 ║");
    Serial.println("║   Smart Panel for Onward Journeys     ║");
    Serial.println("╚═══════════════════════════════════════╝\n");

    logMemory("boot");

    // Load configuration FIRST (needed for display brightness)
    loadConfig(config);

    // Initialize logger with config for debug mode checks (MUST be after loadConfig)
    initLogger(&config);

    // Select transit API based on city configuration
#if defined(MATRIX_PORTAL_M4)
    // M4 only supports Prague (Golemio) API
    transitAPI = &golemioAPI;
    Serial.println("Using Prague Golemio API");
#else
    if (strcmp(config.city, "Berlin") == 0)
    {
        transitAPI = &bvgAPI;
        Serial.println("Using Berlin BVG API");
    }
    else
    {
        transitAPI = &golemioAPI;
        Serial.println("Using Prague Golemio API");
    }
#endif

    // Set up API callbacks
    transitAPI->setStatusCallback(onAPIStatus);
    transitAPI->setPartialResultsCallback(onPartialResults);

    // Initialize display with correct brightness from config
    if (!displayManager.begin(config.brightness))
    {
        debugPrintln("Display initialization failed!");
        return;
    }
    displayManager.setConfig(&config);
    logMemory("display_init");

    displayManager.drawStatus("Starting SpojBoard...", "FW v" FIRMWARE_RELEASE, COLOR_WHITE);

    // Try to connect to WiFi
    bool wifiConnected = wifiManager.connectSTA(config, 20, 500);

    // If noApFallback is true, keep retrying WiFi instead of falling back to AP
    while (!wifiConnected && config.noApFallback)
    {
        displayManager.drawStatus("WiFi Failed!", "Retrying...", COLOR_RED);
        delay(5000);
        wifiConnected = wifiManager.connectSTA(config, 20, 500);
    }

    if (!wifiConnected)
    {
        // Connection failed, start AP mode
        displayManager.drawStatus("WiFi Failed!", "Starting AP mode...", COLOR_RED);
        delay(1500);

        if (!wifiManager.startAP())
        {
            debugPrintln("AP Mode failed to start!");
            displayManager.drawStatus("AP Mode Failed!", "", COLOR_RED);
            return;
        }

        // Start captive portal
        if (!captivePortal.begin(wifiManager.getAPIP()))
        {
            debugPrintln("Captive portal failed to start!");
        }
    }
    else
    {
        // WiFi connected successfully
        char ipStr[32];
        IPAddress ip = WiFi.localIP();
        snprintf(ipStr, sizeof(ipStr), "IP: %d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
        displayManager.drawStatus("WiFi Connected!", ipStr, COLOR_GREEN);
        delay(1500);

        // Start telnet logger if debug mode enabled
        if (config.debugMode)
        {
            TelnetLogger::getInstance().begin(23);
            logTimestamp();
            debugPrintln("Debug mode enabled - telnet logging active");
        }
    }

    // Initialize web server with callbacks
    webServer.setCallbacks(onConfigSave, onRefresh, onReboot, onDemoStart, onDemoStop);
    webServer.setDisplayManager(&displayManager); // For OTA progress updates
    if (!webServer.begin())
    {
        debugPrintln("Web server failed to start!");
    }

    // Setup captive portal detection handlers (ESP32 only - M4 has no web server)
#if !defined(MATRIX_PORTAL_M4)
    if (wifiManager.isAPMode())
    {
        captivePortal.setupDetectionHandlers(webServer.getServer());
    }
#endif

    // Setup NTP time if connected to WiFi
    if (wifiManager.isConnected() && !wifiManager.isAPMode())
    {
        logTimestamp();
        debugPrintln("Syncing time...");
        initTimeSync();

        if (syncTime(10, 500))
        {
            char timeStr[32];
            if (getFormattedTime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S"))
            {
                char msg[64];
                snprintf(msg, sizeof(msg), "Time synced: %s", timeStr);
                logTimestamp();
                debugPrintln(msg);
            }
        }

        // Initial API call if configured
        if (isCityConfigured())
        {
            fetchDepartures();
            lastApiCall = millis(); // Prevent immediate second call in loop()
        }
    }

    needsDisplayUpdate = true;
    logTimestamp();
    debugPrintln("Setup complete!\n");
}

// ============================================================================
// Main Loop
// ============================================================================
void loop()
{
    // Handle DNS for captive portal (AP mode only)
    if (wifiManager.isAPMode())
    {
        captivePortal.processRequests();
    }

    // Handle web server requests
    webServer.handleClient();

    // Process telnet connections if debug enabled
    if (config.debugMode)
    {
        TelnetLogger::getInstance().loop();
    }

    // Update web server state for status display
    webServer.updateState(&config,
                          wifiManager.isConnected(), wifiManager.isAPMode(),
                          wifiManager.getAPSSID(), wifiManager.getAPPassword(), wifiManager.getAPClientCount(),
                          apiError, apiErrorMsg,
                          departureCount, stopName);

    // Skip WiFi monitoring and API calls in AP mode
    if (wifiManager.isAPMode())
    {
        // Update display periodically in AP mode
        if (millis() - lastDisplayUpdate >= 5000)
        {
            lastDisplayUpdate = millis();
            needsDisplayUpdate = true;
        }

        if (needsDisplayUpdate || displayManager.needsRedraw())
        {
            needsDisplayUpdate = false;
            displayManager.updateDisplay(departures, departureCount, config.numDepartures,
                                         wifiManager.isConnected(), wifiManager.isAPMode(),
                                         wifiManager.getAPSSID(), wifiManager.getAPPassword(),
                                         apiError, apiErrorMsg,
                                         stopName, isCityConfigured(),
                                         demoModeActive);
        }

        delay(10);
        return;
    }

    // Check WiFi connection (STA mode only)
    bool isConnected = wifiManager.isConnected();
    static bool wasConnected = isConnected; // Initialize to current state on first call

    if (!isConnected && wasConnected)
    {
        logTimestamp();
        debugPrintln("WiFi: Disconnected!");
        needsDisplayUpdate = true;

        // Attempt reconnection
        static unsigned long lastReconnectAttempt = 0;
        if (millis() - lastReconnectAttempt > 30000)
        {
            lastReconnectAttempt = millis();
            wifiManager.attemptReconnect();
        }
    }
    else if (isConnected && !wasConnected)
    {
        logTimestamp();
        debugPrintln("WiFi: Reconnected!");
        needsDisplayUpdate = true;
    }
    wasConnected = isConnected;

    // Skip API polling and ETA recalculation in demo mode
    if (!demoModeActive)
    {
        // Periodic API calls (only when connected and not in AP mode)
        if (wifiManager.isConnected() && isCityConfigured())
        {
            unsigned long now = millis();
            unsigned long interval = (unsigned long)config.refreshInterval * 1000;

            if (now - lastApiCall >= interval || lastApiCall == 0)
            {
                lastApiCall = now;
                fetchDepartures();
            }
        }

        // Real-time ETA recalculation every 10 seconds (only when connected and have departures)
        if (wifiManager.isConnected() && departureCount > 0)
        {
            unsigned long now = millis();
            if (now - lastEtaRecalc >= 10000 || lastEtaRecalc == 0)
            {
                lastEtaRecalc = now;
                recalculateETAs();
            }
        }
    }

    // Update display
    if (needsDisplayUpdate || displayManager.needsRedraw())
    {
        needsDisplayUpdate = false;
        displayManager.updateDisplay(departures, departureCount, config.numDepartures,
                                     wifiManager.isConnected(), wifiManager.isAPMode(),
                                     wifiManager.getAPSSID(), wifiManager.getAPPassword(),
                                     apiError, apiErrorMsg,
                                     stopName, isCityConfigured(),
                                     demoModeActive);
    }

    // Periodic display update (for time) - now handled by ETA recalc every 10s
    // Removed to avoid redundant updates

    // Status logging every 60 seconds
    static unsigned long lastStatusLog = 0;
    if (millis() - lastStatusLog >= 60000)
    {
        lastStatusLog = millis();
        char statusMsg[128];
        snprintf(statusMsg, sizeof(statusMsg), "STATUS: WiFi=%s | AP=%s | Deps=%d | Heap=%lu",
                 wifiManager.isConnected() ? "OK" : "FAIL",
                 wifiManager.isAPMode() ? "ON" : "OFF",
                 departureCount,
                 (unsigned long)getFreeHeap());
        logTimestamp();
        debugPrintln(statusMsg);
    }

    // Let idle task run
    delay(1);
}