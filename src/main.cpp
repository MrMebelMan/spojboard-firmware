#include <Arduino.h>

// Project modules
#include "utils/Logger.h"
#include "utils/TimeUtils.h"
#include "utils/gfxlatin2.h"
#include "utils/TelnetLogger.h"
#include "config/AppConfig.h"
#include "api/DepartureData.h"
#include "api/GolemioAPI.h"
#include "display/DisplayManager.h"
#include "network/WiFiManager.h"
#include "network/CaptivePortal.h"
#include "network/ConfigWebServer.h"

// Hardware configuration and defaults are now in config/AppConfig.h

// ============================================================================
// Global Objects
// ============================================================================
DisplayManager displayManager;
WiFiManager wifiManager;
CaptivePortal captivePortal;
ConfigWebServer webServer;
GolemioAPI golemioAPI;

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

// ============================================================================
// API Status Callback - Updates display during retries
// ============================================================================
void onAPIStatus(const char* message)
{
    displayManager.drawStatus(message, "", COLOR_YELLOW);
}

// ============================================================================
// ETA Recalculation - Updates ETAs from cached timestamps every 10s
// ============================================================================
void recalculateETAs()
{
    // Recalculate ETAs from cached departureTime timestamps
    // Filter out stale departures (past their departure time)
    time_t now;
    time(&now);

    int validCount = 0;
    for (int i = 0; i < departureCount; i++)
    {
        int diffSec = difftime(departures[i].departureTime, now);
        int eta = (diffSec > 0) ? (diffSec / 60) : 0;

        // Only keep departures that haven't passed yet
        if (eta > 0)
        {
            // Copy departure if we're filtering out previous entries
            if (validCount != i)
            {
                departures[validCount] = departures[i];
            }
            departures[validCount].eta = eta;
            validCount++;
        }
    }

    // Update count if we filtered any departures
    if (validCount != departureCount)
    {
        char msg[64];
        snprintf(msg, sizeof(msg), "ETA Recalc: Filtered %d stale departures", departureCount - validCount);
        logTimestamp();
        debugPrintln(msg);
    }

    departureCount = validCount;

    // Resort departures by ETA after recalculation
    if (departureCount > 1)
    {
        qsort(departures, departureCount, sizeof(Departure), compareDepartures);
    }

    needsDisplayUpdate = true;
}

// ============================================================================
// API Fetch Wrapper - Uses GolemioAPI module
// ============================================================================
void fetchDepartures()
{
    if (!wifiManager.isConnected())
    {
        return;
    }

    // Call API client
    GolemioAPI::APIResult result = golemioAPI.fetchDepartures(config);

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
        ESP.restart();
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
    ESP.restart();
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

    // Initialize display with correct brightness from config
    if (!displayManager.begin(config.brightness))
    {
        debugPrintln("Display initialization failed!");
        return;
    }
    displayManager.setConfig(&config);
    logMemory("display_init");

    // Set up API status callback for display updates
    golemioAPI.setStatusCallback(onAPIStatus);

    displayManager.drawStatus("Starting SpojBoard...", "FW v" FIRMWARE_RELEASE, COLOR_WHITE);

    // Try to connect to WiFi (will fall back to AP mode if fails)
    if (!wifiManager.connectSTA(config, 20, 500))
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
        sprintf(ipStr, "IP: %s", WiFi.localIP().toString().c_str());
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

    // Setup captive portal detection handlers
    if (wifiManager.isAPMode())
    {
        captivePortal.setupDetectionHandlers(webServer.getServer());
    }

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
        if (config.configured && strlen(config.apiKey) > 0)
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

        if (needsDisplayUpdate)
        {
            needsDisplayUpdate = false;
            displayManager.updateDisplay(departures, departureCount, config.numDepartures,
                                         wifiManager.isConnected(), wifiManager.isAPMode(),
                                         wifiManager.getAPSSID(), wifiManager.getAPPassword(),
                                         apiError, apiErrorMsg,
                                         stopName, config.configured && strlen(config.apiKey) > 0,
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
        if (wifiManager.isConnected() && config.configured && strlen(config.apiKey) > 0)
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
    if (needsDisplayUpdate)
    {
        needsDisplayUpdate = false;
        displayManager.updateDisplay(departures, departureCount, config.numDepartures,
                                     wifiManager.isConnected(), wifiManager.isAPMode(),
                                     wifiManager.getAPSSID(), wifiManager.getAPPassword(),
                                     apiError, apiErrorMsg,
                                     stopName, config.configured && strlen(config.apiKey) > 0,
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
        snprintf(statusMsg, sizeof(statusMsg), "STATUS: WiFi=%s | AP=%s | Deps=%d | Heap=%u",
                 wifiManager.isConnected() ? "OK" : "FAIL",
                 wifiManager.isAPMode() ? "ON" : "OFF",
                 departureCount,
                 ESP.getFreeHeap());
        logTimestamp();
        debugPrintln(statusMsg);
    }

    // Let idle task run
    delay(1);
}