#include <Arduino.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>

// Project modules
#include "utils/Logger.h"
#include "utils/TimeUtils.h"
#include "utils/gfxlatin2.h"
#include "config/AppConfig.h"
#include "api/DepartureData.h"
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
// Golemio API Call - Queries each stop separately and sorts results
// ============================================================================
void fetchDepartures()
{
    if (!wifiManager.isConnected() || strlen(config.apiKey) == 0 || strlen(config.stopIds) == 0)
    {
        return;
    }

    logTimestamp();
    Serial.println("API: Fetching departures...");
    logMemory("api_start");

// Temporary array to collect all departures from all stops
#define MAX_TEMP_DEPARTURES 30
    Departure tempDepartures[MAX_TEMP_DEPARTURES];
    int tempCount = 0;

    // Parse comma-separated stop IDs
    char stopIdsCopy[128];
    strlcpy(stopIdsCopy, config.stopIds, sizeof(stopIdsCopy));

    char *stopId = strtok(stopIdsCopy, ",");
    bool firstStop = true;

    while (stopId != NULL && tempCount < MAX_TEMP_DEPARTURES)
    {
        // Trim whitespace
        while (*stopId == ' ')
            stopId++;

        if (strlen(stopId) == 0)
        {
            stopId = strtok(NULL, ",");
            continue;
        }

        logTimestamp();
        Serial.print("API: Querying stop ");
        Serial.println(stopId);

        HTTPClient http;
        char url[512];

        // Query each stop individually with higher total to get more results
        snprintf(url, sizeof(url),
                 "https://api.golemio.cz/v2/pid/departureboards?ids=%s&total=%d&preferredTimezone=Europe/Prague&minutesBefore=0&minutesAfter=120",
                 stopId,
                 config.numDepartures > MAX_DEPARTURES ? MAX_DEPARTURES : config.numDepartures);

        http.begin(url);
        http.addHeader("x-access-token", config.apiKey);
        http.addHeader("Content-Type", "application/json");
        http.setTimeout(10000);

        int httpCode = http.GET();

        if (httpCode == HTTP_CODE_OK)
        {
            String payload = http.getString();
            DynamicJsonDocument doc(8192);
            DeserializationError error = deserializeJson(doc, payload);

            if (error)
            {
                logTimestamp();
                Serial.print("JSON Parse Error for stop ");
                Serial.print(stopId);
                Serial.print(": ");
                Serial.println(error.c_str());
            }
            else
            {
                // Get stop name from first stop (for display)
                if (firstStop && doc.containsKey("stops") && doc["stops"].size() > 0)
                {
                    const char *name = doc["stops"][0]["stop_name"];
                    if (name)
                    {
                        strlcpy(stopName, name, sizeof(stopName));
                        utf8tocp(stopName);
                    }
                    firstStop = false;
                }

                // Parse departures from this stop
                if (doc.containsKey("departures"))
                {
                    JsonArray deps = doc["departures"];

                    for (JsonObject dep : deps)
                    {
                        if (tempCount >= MAX_TEMP_DEPARTURES)
                            break;

                        // Route/Line info
                        const char *line = dep["route"]["short_name"];
                        if (line)
                        {
                            strlcpy(tempDepartures[tempCount].line, line, sizeof(tempDepartures[0].line));
                        }
                        else
                        {
                            tempDepartures[tempCount].line[0] = '\0';
                        }

                        // Destination/Headsign
                        const char *headsign = dep["trip"]["headsign"];
                        if (headsign)
                        {
                            strlcpy(tempDepartures[tempCount].destination, headsign, sizeof(tempDepartures[0].destination));
                            shortenDestination(tempDepartures[tempCount].destination);  // Shorten while still UTF-8
                            utf8tocp(tempDepartures[tempCount].destination);            // Then convert to ISO-8859-2
                        }
                        else
                        {
                            tempDepartures[tempCount].destination[0] = '\0';
                        }

                        // Calculate ETA from departure timestamp
                        const char *timestamp = dep["departure_timestamp"]["predicted"];
                        if (!timestamp)
                        {
                            timestamp = dep["departure_timestamp"]["scheduled"];
                        }

                        if (timestamp)
                        {
                            struct tm tm;
                            strptime(timestamp, "%Y-%m-%dT%H:%M:%S", &tm);
                            time_t depTime = mktime(&tm);
                            time_t now;
                            time(&now);
                            int diffSec = difftime(depTime, now);
                            tempDepartures[tempCount].eta = (diffSec > 0) ? (diffSec / 60) : 0;
                        }
                        else
                        {
                            tempDepartures[tempCount].eta = 0;
                        }

                        // Air conditioning
                        tempDepartures[tempCount].hasAC = dep["trip"]["is_air_conditioned"] | false;

                        // Delay info
                        if (dep.containsKey("delay") && !dep["delay"].isNull())
                        {
                            tempDepartures[tempCount].isDelayed = true;
                            tempDepartures[tempCount].delayMinutes = dep["delay"]["minutes"] | 0;
                        }
                        else
                        {
                            tempDepartures[tempCount].isDelayed = false;
                            tempDepartures[tempCount].delayMinutes = 0;
                        }

                        tempCount++;
                    }
                }
            }
        }
        else
        {
            logTimestamp();
            Serial.print("API Error for stop ");
            Serial.print(stopId);
            Serial.print(": HTTP ");
            Serial.println(httpCode);
        }

        http.end();
        stopId = strtok(NULL, ",");
    }

    // Sort all collected departures by ETA
    if (tempCount > 0)
    {
        qsort(tempDepartures, tempCount, sizeof(Departure), compareDepartures);

        logTimestamp();
        Serial.print("Collected ");
        Serial.print(tempCount);
        Serial.println(" departures from all stops");
    }

    // Filter by minimum departure time and copy to final array
    departureCount = 0;
    for (int i = 0; i < tempCount && departureCount < MAX_DEPARTURES; i++)
    {
        if (tempDepartures[i].eta >= config.minDepartureTime)
        {
            departures[departureCount] = tempDepartures[i];
            departureCount++;
        }
    }

    logTimestamp();
    Serial.print("Final departures after filtering: ");
    Serial.println(departureCount);

    // Set API error status
    apiError = (tempCount == 0);
    if (apiError)
    {
        strlcpy(apiErrorMsg, "No departures", sizeof(apiErrorMsg));
    }

    needsDisplayUpdate = true;
    logMemory("api_complete");
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