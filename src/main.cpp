#include <Arduino.h>

// Project modules
#include "utils/Logger.h"
#include "utils/TimeUtils.h"
#include "utils/gfxlatin2.h"
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
bool needsDisplayUpdate = false;
bool apiError = false;
char apiErrorMsg[64] = "";
char stopName[64] = "";

// Network layer is now in network/ modules:
// - WiFiManager: WiFi connection and AP mode
// - CaptivePortal: DNS server and detection endpoints
// - ConfigWebServer: Web interface handlers

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

void onConfigSave(const Config& newConfig, bool wifiChanged)
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


// ============================================================================
// Setup
// ============================================================================
void setup()
{
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n╔═══════════════════════════════════════╗");
    Serial.println("║   Transport Display - Standalone      ║");
    Serial.println("║   Golemio API Edition                 ║");
    Serial.println("╚═══════════════════════════════════════╝\n");

    logMemory("boot");

    // Load configuration FIRST (needed for display brightness)
    loadConfig(config);

    // Initialize display with correct brightness from config
    if (!displayManager.begin(config.brightness))
    {
        Serial.println("Display initialization failed!");
        return;
    }
    logMemory("display_init");

    displayManager.drawStatus("Starting...", "", COLOR_WHITE);

    // Try to connect to WiFi (will fall back to AP mode if fails)
    if (!wifiManager.connectSTA(config, 20, 500))
    {
        // Connection failed, start AP mode
        displayManager.drawStatus("WiFi Failed!", "Starting AP mode...", COLOR_RED);
        delay(1500);

        if (!wifiManager.startAP())
        {
            Serial.println("AP Mode failed to start!");
            displayManager.drawStatus("AP Mode Failed!", "", COLOR_RED);
            return;
        }

        // Start captive portal
        if (!captivePortal.begin(wifiManager.getAPIP()))
        {
            Serial.println("Captive portal failed to start!");
        }
    }
    else
    {
        // WiFi connected successfully
        char ipStr[32];
        sprintf(ipStr, "IP: %s", WiFi.localIP().toString().c_str());
        displayManager.drawStatus("WiFi Connected!", ipStr, COLOR_GREEN);
        delay(1500);
    }

    // Initialize web server with callbacks
    webServer.setCallbacks(onConfigSave, onRefresh, onReboot);
    webServer.setDisplayManager(&displayManager);  // For OTA progress updates
    if (!webServer.begin())
    {
        Serial.println("Web server failed to start!");
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
        Serial.println("Syncing time...");
        initTimeSync();

        if (syncTime(10, 500))
        {
            char timeStr[32];
            if (getFormattedTime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S"))
            {
                logTimestamp();
                Serial.print("Time synced: ");
                Serial.println(timeStr);
            }
        }

        // Initial API call if configured
        if (config.configured && strlen(config.apiKey) > 0)
        {
            fetchDepartures();
        }
    }

    needsDisplayUpdate = true;
    logTimestamp();
    Serial.println("Setup complete!\n");
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
                                        stopName, config.configured && strlen(config.apiKey) > 0);
        }

        delay(10);
        return;
    }

    // Check WiFi connection (STA mode only)
    static bool wasConnected = false;
    bool isConnected = wifiManager.isConnected();

    if (!isConnected && wasConnected)
    {
        logTimestamp();
        Serial.println("WiFi: Disconnected!");
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
        Serial.println("WiFi: Reconnected!");
        needsDisplayUpdate = true;
    }
    wasConnected = isConnected;

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

    // Update display
    if (needsDisplayUpdate)
    {
        needsDisplayUpdate = false;
        displayManager.updateDisplay(departures, departureCount, config.numDepartures,
                                    wifiManager.isConnected(), wifiManager.isAPMode(),
                                    wifiManager.getAPSSID(), wifiManager.getAPPassword(),
                                    apiError, apiErrorMsg,
                                    stopName, config.configured && strlen(config.apiKey) > 0);
    }

    // Periodic display update (for time)
    if (millis() - lastDisplayUpdate >= 30000)
    {
        lastDisplayUpdate = millis();
        needsDisplayUpdate = true;
    }

    // Status logging every 60 seconds
    static unsigned long lastStatusLog = 0;
    if (millis() - lastStatusLog >= 60000)
    {
        lastStatusLog = millis();
        logTimestamp();
        Serial.print("STATUS: WiFi=");
        Serial.print(wifiManager.isConnected() ? "OK" : "FAIL");
        Serial.print(" | AP=");
        Serial.print(wifiManager.isAPMode() ? "ON" : "OFF");
        Serial.print(" | Deps=");
        Serial.print(departureCount);
        Serial.print(" | Heap=");
        Serial.println(ESP.getFreeHeap());
    }

    // Let idle task run
    delay(1);
}