#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <Preferences.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <ArduinoJson.h>
#include <time.h>

// Custom 8-bit fonts with full ISO-8859-2 support for Czech characters
#include "../fonts/DepartureMono4pt8b.h"
#include "../fonts/DepartureMono5pt8b.h"

// UTF-8 to ISO-8859-2 conversion for Czech characters
#include "decodeutf8.h"
#include "gfxlatin2.h"

// ============================================================================
// Configuration - Edit these for initial setup (can be changed via web UI)
// ============================================================================
#define DEFAULT_WIFI_SSID "Your WiFi SSID"
#define DEFAULT_WIFI_PASSWORD "Your WiFi Password"

// ============================================================================
// HUB75 Display Configuration (Adafruit MatrixPortal ESP32-S3)
// ============================================================================
#define PANEL_WIDTH 64
#define PANEL_HEIGHT 32
#define PANELS_NUMBER 2 // 128x32 total

// Pin Mapping for Adafruit MatrixPortal ESP32-S3
#define R1_PIN 42
#define G1_PIN 40
#define B1_PIN 41
#define R2_PIN 38
#define G2_PIN 37
#define B2_PIN 39

#define A_PIN 45
#define B_PIN 36
#define C_PIN 48
#define D_PIN 35
#define E_PIN 21

#define LAT_PIN 47
#define OE_PIN 14
#define CLK_PIN 2

// ============================================================================
// Global Objects
// ============================================================================
MatrixPanel_I2S_DMA *display = nullptr;
WebServer server(80);
Preferences preferences;

// Font references - Using 8-bit fonts for full Czech character support
const GFXfont *fontSmall = &DepartureMono_Regular4pt8b;  // 8-bit font with ISO-8859-2 encoding
const GFXfont *fontMedium = &DepartureMono_Regular5pt8b; // 8-bit font with ISO-8859-2 encoding

// ============================================================================
// Configuration Storage
// ============================================================================
struct Config
{
    char wifiSsid[64];
    char wifiPassword[64];
    char apiKey[300];
    char stopIds[128];    // Comma-separated stop IDs (e.g., "U693Z2P,U693Z1P")
    int refreshInterval;  // Seconds between API calls
    int numDepartures;    // Number of departures to fetch
    int minDepartureTime; // Minimum departure time in minutes (filter out departures < this)
    bool configured;
};

Config config;

// ============================================================================
// Departure Data
// ============================================================================
struct Departure
{
    char line[8];         // Line number (e.g., "31", "A", "S9")
    char destination[32]; // Destination/headsign
    int eta;              // Minutes until departure
    bool hasAC;           // Air conditioning
    bool isDelayed;       // Has delay
    int delayMinutes;     // Delay in minutes
};

#define MAX_DEPARTURES 6
Departure departures[MAX_DEPARTURES];
int departureCount = 0;

// ============================================================================
// State Variables
// ============================================================================
unsigned long lastApiCall = 0;
unsigned long lastDisplayUpdate = 0;
bool needsDisplayUpdate = false;
bool isDrawing = false;
bool wifiConnected = false;
bool apModeActive = false;
bool apiError = false;
char apiErrorMsg[64] = "";
char stopName[64] = "";

// AP Mode Configuration
#define AP_SSID_PREFIX "TransportDisplay-"
#define WIFI_CONNECT_ATTEMPTS 20
char apSSID[32];
char apPassword[9]; // 8 chars + null

// DNS Server for captive portal
#include <DNSServer.h>
DNSServer dnsServer;
const byte DNS_PORT = 53;

// ============================================================================
// Colors
// ============================================================================
uint16_t COLOR_WHITE;
uint16_t COLOR_YELLOW;
uint16_t COLOR_RED;
uint16_t COLOR_GREEN;
uint16_t COLOR_BLUE;
uint16_t COLOR_ORANGE;
uint16_t COLOR_PURPLE;
uint16_t COLOR_BLACK;
uint16_t COLOR_CYAN;

// ============================================================================
// Time Configuration
// ============================================================================
struct tm timeinfo;
const char *ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 3600;     // CET = UTC+1
const int daylightOffset_sec = 3600; // CEST = UTC+2

// ============================================================================
// Debug Logging
// ============================================================================
void logTimestamp()
{
    char timestamp[24];
    sprintf(timestamp, "[%010lu] ", millis());
    Serial.print(timestamp);
}

void logMemory(const char *location)
{
    logTimestamp();
    Serial.print("MEM@");
    Serial.print(location);
    Serial.print(": Free=");
    Serial.print(ESP.getFreeHeap());
    Serial.print(" Min=");
    Serial.println(ESP.getMinFreeHeap());
}

// ============================================================================
// Configuration Management
// ============================================================================
void loadConfig()
{
    preferences.begin("transport", true); // Read-only

    strlcpy(config.wifiSsid, preferences.getString("wifiSsid", DEFAULT_WIFI_SSID).c_str(), sizeof(config.wifiSsid));
    strlcpy(config.wifiPassword, preferences.getString("wifiPass", DEFAULT_WIFI_PASSWORD).c_str(), sizeof(config.wifiPassword));
    strlcpy(config.apiKey, preferences.getString("apiKey", "").c_str(), sizeof(config.apiKey));
    strlcpy(config.stopIds, preferences.getString("stopIds", "U693Z2P").c_str(), sizeof(config.stopIds));
    config.refreshInterval = preferences.getInt("refresh", 30);
    config.numDepartures = preferences.getInt("numDeps", 3);
    config.minDepartureTime = preferences.getInt("minDepTime", 3);
    config.configured = preferences.getBool("configured", false);

    preferences.end();

    logTimestamp();
    Serial.println("Config loaded:");
    Serial.print("  SSID: ");
    Serial.println(config.wifiSsid);
    Serial.print("  Stop IDs: ");
    Serial.println(config.stopIds);
    Serial.print("  Refresh: ");
    Serial.print(config.refreshInterval);
    Serial.println("s");
    Serial.print("  Configured: ");
    Serial.println(config.configured ? "Yes" : "No");
}

void saveConfig()
{
    preferences.begin("transport", false); // Read-write

    preferences.putString("wifiSsid", config.wifiSsid);
    preferences.putString("wifiPass", config.wifiPassword);
    preferences.putString("apiKey", config.apiKey);
    preferences.putString("stopIds", config.stopIds);
    preferences.putInt("refresh", config.refreshInterval);
    preferences.putInt("numDeps", config.numDepartures);
    preferences.putInt("minDepTime", config.minDepartureTime);
    preferences.putBool("configured", true);

    preferences.end();

    logTimestamp();
    Serial.println("Config saved");
}

// ============================================================================
// Display Setup
// ============================================================================
void setup_display()
{
    HUB75_I2S_CFG::i2s_pins _pins = {
        R1_PIN, G1_PIN, B1_PIN, R2_PIN, G2_PIN, B2_PIN,
        A_PIN, B_PIN, C_PIN, D_PIN, E_PIN,
        LAT_PIN, OE_PIN, CLK_PIN};

    HUB75_I2S_CFG mxconfig(
        PANEL_WIDTH,
        PANEL_HEIGHT,
        PANELS_NUMBER,
        _pins);

    mxconfig.clkphase = false;
    mxconfig.i2sspeed = HUB75_I2S_CFG::HZ_10M;

    display = new MatrixPanel_I2S_DMA(mxconfig);

    if (!display->begin())
    {
        Serial.println("Display FAILED!");
        while (1)
            ;
    }

    display->setBrightness8(90);
    display->clearScreen();

    // Define colors
    COLOR_WHITE = display->color565(255, 255, 255);
    COLOR_YELLOW = display->color565(255, 255, 0);
    COLOR_RED = display->color565(255, 0, 0);
    COLOR_GREEN = display->color565(0, 255, 0);
    COLOR_BLUE = display->color565(0, 0, 255);
    COLOR_ORANGE = display->color565(255, 165, 0);
    COLOR_PURPLE = display->color565(128, 0, 128);
    COLOR_BLACK = display->color565(0, 0, 0);
    COLOR_CYAN = display->color565(0, 255, 255);
}

// ============================================================================
// Line Color Helper
// ============================================================================
uint16_t getLineColor(const char *line)
{
    // Metro lines
    if (strcmp(line, "A") == 0)
        return COLOR_GREEN;
    if (strcmp(line, "B") == 0)
        return COLOR_YELLOW;
    if (strcmp(line, "C") == 0)
        return COLOR_RED;

    // Tram lines
    if (strcmp(line, "8") == 0)
        return COLOR_RED;
    if (strcmp(line, "7") == 0)
        return COLOR_ORANGE;
    if (strcmp(line, "12") == 0)
        return COLOR_PURPLE;
    if (strcmp(line, "31") == 0)
        return COLOR_GREEN;

    // S-trains
    if (line[0] == 'S')
        return COLOR_BLUE;

    // Night lines
    if (line[0] == '9' && strlen(line) <= 2)
        return COLOR_CYAN; // Night trams 91-99

    return COLOR_YELLOW; // Default
}

// ============================================================================
// Draw Departure Row
// ============================================================================
void drawDeparture(int row, Departure dep)
{
    int y = row * 8; // Each row is 8 pixels

    // Draw line number background - always black
    uint16_t lineColor = getLineColor(dep.line);
    int bgWidth = (strlen(dep.line) > 2) ? 18 : 14;
    display->fillRect(1, y + 1, bgWidth, 7, COLOR_BLACK);

    // Line number text - colored text on black background
    display->setTextColor(lineColor);
    display->setFont(fontMedium);

    // Center the line number text within the background rectangle
    int16_t x1, y1;
    uint16_t w, h;
    display->getTextBounds(dep.line, 0, 0, &x1, &y1, &w, &h);
    // Account for font's left bearing offset (x1) when centering
    int textX = 1 + (bgWidth - w) / 2 - x1;
    // Align baseline with destination (y + 7)
    display->setCursor(textX, y + 7);
    display->print(dep.line);

    // AC indicator (asterisk before destination)
    int destX = bgWidth + 4;
    if (dep.hasAC)
    {
        display->setTextColor(COLOR_CYAN);
        display->setCursor(destX, y + 7);
        display->print("*");
        destX += 6;
    }

    // Destination
    display->setTextColor(COLOR_WHITE);
    display->setFont(fontMedium);
    display->setCursor(destX, y + 7);

    // Truncate destination if too long
    char destTrunc[20];
    int maxChars = dep.hasAC ? 14 : 15;
    strncpy(destTrunc, dep.destination, maxChars);
    destTrunc[maxChars] = '\0';
    display->print(destTrunc);

    // ETA display
    int etaCursor = 111;
    if (dep.eta >= 100)
    {
        etaCursor = 105;
    }
    else if (dep.eta >= 10)
    {
        etaCursor = 111;
    }
    else
    {
        etaCursor = 117;
    }

    display->setCursor(etaCursor, y + 7);

    // Color based on ETA
    if (dep.eta < 2)
    {
        display->setTextColor(COLOR_RED);
    }
    else if (dep.eta < 5)
    {
        display->setTextColor(COLOR_YELLOW);
    }
    else
    {
        display->setTextColor(COLOR_WHITE);
    }

    // Show delay indicator
    if (dep.isDelayed && dep.delayMinutes > 0)
    {
        display->setTextColor(COLOR_ORANGE);
    }

    if (dep.eta < 1)
    {
        display->print("<1'");
    }
    else if (dep.eta >= 60)
    {
        display->print(">1h");
    }
    else
    {
        display->print(dep.eta);
        display->print("'");
    }
}

// ============================================================================
// Draw Date/Time Row
// ============================================================================
void drawDateTime()
{
    int y = 24; // Bottom row

    if (!getLocalTime(&timeinfo))
    {
        display->setTextColor(COLOR_RED);
        display->setFont(fontSmall);
        display->setCursor(2, y + 7);
        display->print("Time Sync...");
        return;
    }

    display->setFont(fontSmall);
    display->setTextColor(COLOR_WHITE);

    // Day of week
    char dayStr[5];
    strftime(dayStr, 5, "%a,", &timeinfo);
    utf8tocp(dayStr); // Convert Czech day names
    display->setCursor(2, y + 7);
    display->print(dayStr);

    // Date
    char dateStr[7];
    strftime(dateStr, 7, "%b %d", &timeinfo);
    utf8tocp(dateStr); // Convert Czech month names
    display->setCursor(22, y + 7);
    display->print(dateStr);

    // Time
    char timeStr[6];
    strftime(timeStr, 6, "%H:%M", &timeinfo);
    display->setCursor(102, y + 7);
    display->print(timeStr);
}

// ============================================================================
// Draw Status Screen
// ============================================================================
void drawStatus(const char *line1, const char *line2, uint16_t color)
{
    display->clearScreen();
    display->setTextColor(color);
    display->setFont(fontMedium);

    if (line1)
    {
        display->setCursor(2, 12);
        display->print(line1);
    }
    if (line2)
    {
        display->setCursor(2, 24);
        display->print(line2);
    }
}

// ============================================================================
// Update Display
// ============================================================================
void updateDisplay()
{
    if (isDrawing)
        return;

    isDrawing = true;
    display->clearScreen();
    delay(1);

    // AP Mode - Show credentials
    if (apModeActive)
    {
        display->setFont(fontSmall);

        // Title
        display->setTextColor(COLOR_CYAN);
        display->setCursor(2, 7);
        display->print("WiFi Setup Mode");

        // SSID
        display->setTextColor(COLOR_WHITE);
        display->setCursor(2, 15);
        display->print("SSID:");
        display->setTextColor(COLOR_YELLOW);
        display->setCursor(32, 15);
        display->print(apSSID);

        // Password
        display->setTextColor(COLOR_WHITE);
        display->setCursor(2, 23);
        display->print("Pass:");
        display->setTextColor(COLOR_GREEN);
        display->setCursor(32, 23);
        display->print(apPassword);

        // IP
        display->setTextColor(COLOR_WHITE);
        display->setCursor(2, 31);
        display->print("Go to: 192.168.4.1");

        isDrawing = false;
        return;
    }

    if (!wifiConnected)
    {
        drawStatus("WiFi Connecting...", config.wifiSsid, COLOR_YELLOW);
        isDrawing = false;
        return;
    }

    if (!config.configured || strlen(config.apiKey) == 0)
    {
        char ipStr[32];
        sprintf(ipStr, "http://%s", WiFi.localIP().toString().c_str());
        drawStatus("Setup Required", ipStr, COLOR_CYAN);
        isDrawing = false;
        return;
    }

    if (apiError)
    {
        drawStatus("API Error", apiErrorMsg, COLOR_RED);
        drawDateTime();
        isDrawing = false;
        return;
    }

    if (departureCount == 0)
    {
        drawStatus("No Departures", stopName[0] ? stopName : "Waiting...", COLOR_YELLOW);
        drawDateTime();
        isDrawing = false;
        return;
    }

    // Draw departures (top 3 rows)
    for (int i = 0; i < departureCount && i < 3; i++)
    {
        drawDeparture(i, departures[i]);
        delay(1);
    }

    drawDateTime();
    delay(1);

    isDrawing = false;
}

// ============================================================================
// String Shortening for Display
// ============================================================================
struct StringReplacement
{
    const char *search;
    const char *replace;
};

// Common Czech words to shorten for display space
// Note: Strings are in UTF-8 format (before conversion to ISO-8859-2)
const StringReplacement replacements[] = {
    {"Nádraží", "Nádr."},
    // Add more replacements here as needed
    {"Sídliště", "Sídl."},
    {"Nemocnice", "Nem."},
};
const int replacementCount = sizeof(replacements) / sizeof(replacements[0]);

void shortenDestination(char *destination)
{
    for (int i = 0; i < replacementCount; i++)
    {
        char *pos = strstr(destination, replacements[i].search);
        if (pos != NULL)
        {
            int searchLen = strlen(replacements[i].search);
            int replaceLen = strlen(replacements[i].replace);
            int tailLen = strlen(pos + searchLen);

            // Copy replacement
            memcpy(pos, replacements[i].replace, replaceLen);
            // Shift remaining text
            memmove(pos + replaceLen, pos + searchLen, tailLen + 1); // +1 for null terminator
        }
    }
}

// ============================================================================
// Departure Sorting Helper
// ============================================================================
int compareDepartures(const void *a, const void *b)
{
    Departure *depA = (Departure *)a;
    Departure *depB = (Departure *)b;
    return depA->eta - depB->eta; // Sort by ETA ascending
}

// ============================================================================
// Golemio API Call - Queries each stop separately and sorts results
// ============================================================================
void fetchDepartures()
{
    if (!wifiConnected || strlen(config.apiKey) == 0 || strlen(config.stopIds) == 0)
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
// Web Server Handlers
// ============================================================================

// HTML page template
const char *HTML_HEADER = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>Transport Display Config</title>
    <style>
        body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; 
               max-width: 600px; margin: 0 auto; padding: 20px; background: #1a1a2e; color: #eee; }
        h1 { color: #00d4ff; }
        h2 { color: #ff6b6b; margin-top: 30px; }
        .card { background: #16213e; border-radius: 10px; padding: 20px; margin: 15px 0; }
        label { display: block; margin: 10px 0 5px; color: #aaa; font-size: 0.9em; }
        input, select { width: 100%; padding: 12px; border: 1px solid #333; border-radius: 5px; 
                        background: #0f0f23; color: #fff; box-sizing: border-box; font-size: 16px; }
        input:focus { border-color: #00d4ff; outline: none; }
        button { background: #00d4ff; color: #000; padding: 15px 30px; border: none; 
                 border-radius: 5px; cursor: pointer; font-size: 16px; margin-top: 20px; width: 100%; }
        button:hover { background: #00a8cc; }
        button.danger { background: #ff4757; color: #fff; }
        button.danger:hover { background: #ff6b81; }
        .status { padding: 15px; border-radius: 5px; margin: 10px 0; }
        .status.ok { background: #2ed573; color: #000; }
        .status.error { background: #ff4757; }
        .status.warn { background: #ffa502; color: #000; }
        .info { color: #888; font-size: 0.85em; margin-top: 5px; }
        a { color: #00d4ff; }
        .grid { display: grid; grid-template-columns: 1fr 1fr; gap: 15px; }
        @media (max-width: 500px) { .grid { grid-template-columns: 1fr; } }
    </style>
</head>
<body>
)rawliteral";

const char *HTML_FOOTER = "</body></html>";

void handleRoot()
{
    String html = HTML_HEADER;
    html += "<h1>🚌 Transport Display</h1>";

    // AP Mode banner
    if (apModeActive)
    {
        html += "<div class='card' style='background: #ff6b6b; color: #fff;'>";
        html += "<h2 style='color: #fff; margin-top: 0;'>⚠️ Setup Mode</h2>";
        html += "<p>Device is in Access Point mode. Configure WiFi credentials below to connect to your network.</p>";
        html += "<p><strong>AP Name:</strong> " + String(apSSID) + "</p>";
        html += "</div>";
    }

    // Status card
    html += "<div class='card'>";
    html += "<h2>Status</h2>";

    if (apModeActive)
    {
        html += "<div class='status warn'>AP Mode Active - Not connected to WiFi</div>";
        html += "<p><strong>Connected clients:</strong> " + String(WiFi.softAPgetStationNum()) + "</p>";
    }
    else if (wifiConnected)
    {
        html += "<div class='status ok'>WiFi Connected: " + WiFi.localIP().toString() + "</div>";
    }
    else
    {
        html += "<div class='status error'>WiFi Disconnected</div>";
    }

    if (!apModeActive)
    {
        if (strlen(config.apiKey) > 0)
        {
            if (apiError)
            {
                html += "<div class='status error'>API Error: " + String(apiErrorMsg) + "</div>";
            }
            else
            {
                html += "<div class='status ok'>API OK - " + String(departureCount) + " departures</div>";
            }
        }
        else
        {
            html += "<div class='status warn'>API Key not configured</div>";
        }

        if (stopName[0])
        {
            html += "<p><strong>Stop:</strong> " + String(stopName) + "</p>";
        }
    }

    html += "<p><strong>Free Memory:</strong> " + String(ESP.getFreeHeap()) + " bytes</p>";
    html += "</div>";

    // Configuration form
    html += "<div class='card'>";
    html += "<h2>Configuration</h2>";
    html += "<form method='POST' action='/save'>";

    html += "<label>WiFi SSID</label>";
    html += "<input type='text' name='ssid' value='" + String(config.wifiSsid) + "' required placeholder='Your WiFi network name'>";

    html += "<label>WiFi Password</label>";
    html += "<input type='password' name='password' placeholder='Enter WiFi password'>";
    if (!apModeActive)
    {
        html += "<p class='info'>Leave empty to keep current password</p>";
    }
    else
    {
        html += "<p class='info'>Enter your WiFi password</p>";
    }

    html += "<label>Golemio API Key</label>";
    html += "<input type='text' name='apikey' value='" + String(config.apiKey) + "' required placeholder='Your Golemio API key'>";
    html += "<p class='info'>Get your API key at <a href='https://api.golemio.cz/api-keys/' target='_blank'>api.golemio.cz</a></p>";

    html += "<label>Stop ID(s)</label>";
    html += "<input type='text' name='stops' value='" + String(config.stopIds) + "' required placeholder='e.g., U693Z2P'>";
    html += "<p class='info'>Comma-separated PID stop IDs. Find IDs at <a href='http://data.pid.cz/stops/xml/StopsByName.xml' target='_blank'>PID data</a></p>";

    html += "<div class='grid'>";
    html += "<div><label>Refresh Interval (sec)</label>";
    html += "<input type='number' name='refresh' value='" + String(config.refreshInterval) + "' min='10' max='300'></div>";

    html += "<div><label>Number of Departures</label>";
    html += "<input type='number' name='numdeps' value='" + String(config.numDepartures) + "' min='1' max='6'></div>";

    html += "<div><label>Min Departure Time (min)</label>";
    html += "<input type='number' name='mindeptime' value='" + String(config.minDepartureTime) + "' min='0' max='30'></div>";
    html += "</div>";

    if (apModeActive)
    {
        html += "<button type='submit'>Save & Connect to WiFi</button>";
    }
    else
    {
        html += "<button type='submit'>Save Configuration</button>";
    }
    html += "</form>";
    html += "</div>";

    // Actions
    if (!apModeActive)
    {
        html += "<div class='card'>";
        html += "<h2>Actions</h2>";
        html += "<form method='POST' action='/refresh' style='display:inline'>";
        html += "<button type='submit'>Refresh Now</button>";
        html += "</form>";
        html += "<form method='POST' action='/reboot' style='display:inline; margin-top:10px'>";
        html += "<button type='submit' class='danger'>Reboot Device</button>";
        html += "</form>";
        html += "</div>";
    }

    html += HTML_FOOTER;
    server.send(200, "text/html", html);
}

void handleSave()
{
    bool wifiChanged = false;

    if (server.hasArg("ssid"))
    {
        String newSsid = server.arg("ssid");
        if (newSsid != config.wifiSsid)
        {
            wifiChanged = true;
        }
        strlcpy(config.wifiSsid, newSsid.c_str(), sizeof(config.wifiSsid));
    }
    if (server.hasArg("password") && server.arg("password").length() > 0)
    {
        strlcpy(config.wifiPassword, server.arg("password").c_str(), sizeof(config.wifiPassword));
        wifiChanged = true;
    }
    if (server.hasArg("apikey"))
    {
        strlcpy(config.apiKey, server.arg("apikey").c_str(), sizeof(config.apiKey));
    }
    if (server.hasArg("stops"))
    {
        strlcpy(config.stopIds, server.arg("stops").c_str(), sizeof(config.stopIds));
    }
    if (server.hasArg("refresh"))
    {
        config.refreshInterval = server.arg("refresh").toInt();
        if (config.refreshInterval < 10)
            config.refreshInterval = 10;
        if (config.refreshInterval > 300)
            config.refreshInterval = 300;
    }
    if (server.hasArg("numdeps"))
    {
        config.numDepartures = server.arg("numdeps").toInt();
        if (config.numDepartures < 1)
            config.numDepartures = 1;
        if (config.numDepartures > MAX_DEPARTURES)
            config.numDepartures = MAX_DEPARTURES;
    }
    if (server.hasArg("mindeptime"))
    {
        config.minDepartureTime = server.arg("mindeptime").toInt();
        if (config.minDepartureTime < 0)
            config.minDepartureTime = 0;
        if (config.minDepartureTime > 30)
            config.minDepartureTime = 30;
    }

    config.configured = true;
    saveConfig();

    // If in AP mode or WiFi changed, attempt to connect to the new network
    if (apModeActive || wifiChanged)
    {
        String html = HTML_HEADER;
        html += "<h1>⏳ Connecting...</h1>";
        html += "<p>Attempting to connect to WiFi network: <strong>" + String(config.wifiSsid) + "</strong></p>";
        html += "<p>Please wait... The device will restart and connect to the new network.</p>";
        html += "<p>If connection fails, the device will return to AP mode.</p>";
        html += "<div class='card'>";
        html += "<p>After successful connection, access the device at its new IP address on your network.</p>";
        html += "</div>";
        html += HTML_FOOTER;
        server.send(200, "text/html", html);

        delay(1000);

        // Restart to apply new WiFi settings
        ESP.restart();
    }
    else
    {
        // Normal save without WiFi change
        String html = HTML_HEADER;
        html += "<h1>✅ Configuration Saved</h1>";
        html += "<p>Settings have been saved. The device will apply them immediately.</p>";
        html += "<p><a href='/'>← Back to Dashboard</a></p>";
        html += HTML_FOOTER;
        server.send(200, "text/html", html);

        // Trigger immediate refresh
        lastApiCall = 0;
    }
}

void handleRefresh()
{
    lastApiCall = 0; // Force immediate refresh
    server.sendHeader("Location", "/");
    server.send(302, "text/plain", "");
}

void handleReboot()
{
    String html = HTML_HEADER;
    html += "<h1>🔄 Rebooting...</h1>";
    html += "<p>The device is rebooting. Please wait a few seconds.</p>";
    html += "<script>setTimeout(function(){ window.location='/'; }, 5000);</script>";
    html += HTML_FOOTER;
    server.send(200, "text/html", html);
    delay(500);
    ESP.restart();
}

void handleNotFound()
{
    // Captive portal redirect - redirect all unknown requests to root
    if (apModeActive)
    {
        server.sendHeader("Location", "http://192.168.4.1/");
        server.send(302, "text/plain", "");
    }
    else
    {
        server.sendHeader("Location", "/");
        server.send(302, "text/plain", "");
    }
}

// ============================================================================
// AP Mode Functions
// ============================================================================
void generateRandomPassword()
{
    // Generate 8-character alphanumeric password
    const char charset[] = "abcdefghjkmnpqrstuvwxyz23456789"; // Excluded confusing chars: i,l,o,0,1
    randomSeed(esp_random());                                 // Use hardware RNG

    for (int i = 0; i < 8; i++)
    {
        apPassword[i] = charset[random(0, strlen(charset))];
    }
    apPassword[8] = '\0';
}

void generateAPName()
{
    // Create unique AP name using last 4 chars of MAC
    uint8_t mac[6];
    WiFi.macAddress(mac);
    snprintf(apSSID, sizeof(apSSID), "%s%02X%02X", AP_SSID_PREFIX, mac[4], mac[5]);
}

void startAPMode()
{
    logTimestamp();
    Serial.println("Starting AP Mode...");

    // Generate credentials
    generateAPName();
    generateRandomPassword();

    // Stop any existing WiFi connection
    WiFi.disconnect(true);
    delay(100);

    // Configure AP
    WiFi.mode(WIFI_AP);
    WiFi.softAP(apSSID, apPassword);

    // Configure AP IP (default is 192.168.4.1)
    IPAddress apIP(192, 168, 4, 1);
    IPAddress gateway(192, 168, 4, 1);
    IPAddress subnet(255, 255, 255, 0);
    WiFi.softAPConfig(apIP, gateway, subnet);

    // Start DNS server for captive portal (redirect all domains to AP IP)
    dnsServer.start(DNS_PORT, "*", apIP);

    apModeActive = true;
    wifiConnected = false;

    logTimestamp();
    Serial.println("AP Mode Active!");
    Serial.print("  SSID: ");
    Serial.println(apSSID);
    Serial.print("  Password: ");
    Serial.println(apPassword);
    Serial.print("  IP: ");
    Serial.println(WiFi.softAPIP());

    // Update display with AP credentials
    needsDisplayUpdate = true;
}

void stopAPMode()
{
    if (apModeActive)
    {
        logTimestamp();
        Serial.println("Stopping AP Mode...");

        dnsServer.stop();
        WiFi.softAPdisconnect(true);
        apModeActive = false;

        delay(100);
    }
}

// Captive portal detection endpoints
void handleCaptivePortal()
{
    server.sendHeader("Location", "http://192.168.4.1/");
    server.send(302, "text/plain", "");
}

void setupCaptivePortalHandlers()
{
    // Android captive portal detection
    server.on("/generate_204", handleCaptivePortal);
    server.on("/gen_204", handleCaptivePortal);

    // iOS/macOS captive portal detection
    server.on("/hotspot-detect.html", handleCaptivePortal);
    server.on("/library/test/success.html", handleCaptivePortal);

    // Windows captive portal detection
    server.on("/ncsi.txt", handleCaptivePortal);
    server.on("/connecttest.txt", handleCaptivePortal);
    server.on("/redirect", handleCaptivePortal);

    // Firefox captive portal detection
    server.on("/success.txt", handleCaptivePortal);
}

void setupWebServer()
{
    server.on("/", HTTP_GET, handleRoot);
    server.on("/save", HTTP_POST, handleSave);
    server.on("/refresh", HTTP_POST, handleRefresh);
    server.on("/reboot", HTTP_POST, handleReboot);

    // Captive portal handlers
    setupCaptivePortalHandlers();

    server.onNotFound(handleNotFound);
    server.begin();

    logTimestamp();
    Serial.println("Web server started");
}

// ============================================================================
// WiFi Connection
// ============================================================================
void connectWiFi()
{
    logTimestamp();
    Serial.print("WiFi: Connecting to ");
    Serial.println(config.wifiSsid);

    drawStatus("Connecting WiFi...", config.wifiSsid, COLOR_YELLOW);

    WiFi.mode(WIFI_STA);
    WiFi.begin(config.wifiSsid, config.wifiPassword);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < WIFI_CONNECT_ATTEMPTS)
    {
        delay(500);
        Serial.print(".");

        // Update display with attempt count
        if (attempts % 4 == 0)
        {
            char msg[32];
            snprintf(msg, sizeof(msg), "Attempt %d/%d", attempts + 1, WIFI_CONNECT_ATTEMPTS);
            drawStatus("Connecting WiFi...", msg, COLOR_YELLOW);
        }

        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED)
    {
        wifiConnected = true;
        apModeActive = false;
        logTimestamp();
        Serial.println("\nWiFi: Connected!");
        Serial.print("IP: ");
        Serial.println(WiFi.localIP());

        char ipStr[32];
        sprintf(ipStr, "IP: %s", WiFi.localIP().toString().c_str());
        drawStatus("WiFi Connected!", ipStr, COLOR_GREEN);
        delay(1500);
    }
    else
    {
        wifiConnected = false;
        logTimestamp();
        Serial.println("\nWiFi: Connection failed!");
        Serial.println("Starting AP mode for configuration...");

        drawStatus("WiFi Failed!", "Starting AP mode...", COLOR_RED);
        delay(1500);

        // Fall back to AP mode
        startAPMode();
    }
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

    // Initialize display
    setup_display();
    logMemory("display_init");

    drawStatus("Starting...", "", COLOR_WHITE);

    // Load configuration
    loadConfig();

    // Connect to WiFi (will fall back to AP mode if fails)
    connectWiFi();

    // Start web server (works in both STA and AP mode)
    setupWebServer();

    if (wifiConnected && !apModeActive)
    {
        // Setup NTP time
        configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

        // Wait for time sync
        logTimestamp();
        Serial.println("Syncing time...");
        int timeAttempts = 0;
        while (!getLocalTime(&timeinfo) && timeAttempts < 10)
        {
            delay(500);
            timeAttempts++;
        }

        if (getLocalTime(&timeinfo))
        {
            char timeStr[32];
            strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
            logTimestamp();
            Serial.print("Time synced: ");
            Serial.println(timeStr);
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
    if (apModeActive)
    {
        dnsServer.processNextRequest();
    }

    // Handle web server requests
    server.handleClient();

    // Skip WiFi monitoring and API calls in AP mode
    if (apModeActive)
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
            updateDisplay();
        }

        delay(10);
        return;
    }

    // Check WiFi connection (STA mode only)
    if (WiFi.status() != WL_CONNECTED && wifiConnected)
    {
        wifiConnected = false;
        logTimestamp();
        Serial.println("WiFi: Disconnected!");
        needsDisplayUpdate = true;

        // Attempt reconnection
        static unsigned long lastReconnectAttempt = 0;
        if (millis() - lastReconnectAttempt > 30000)
        { // Try every 30 seconds
            lastReconnectAttempt = millis();
            logTimestamp();
            Serial.println("WiFi: Attempting reconnection...");
            WiFi.reconnect();
        }
    }
    else if (WiFi.status() == WL_CONNECTED && !wifiConnected)
    {
        wifiConnected = true;
        logTimestamp();
        Serial.println("WiFi: Reconnected!");
        needsDisplayUpdate = true;
    }

    // Periodic API calls (only when connected and not in AP mode)
    if (wifiConnected && config.configured && strlen(config.apiKey) > 0)
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
        updateDisplay();
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
        Serial.print(wifiConnected ? "OK" : "FAIL");
        Serial.print(" | AP=");
        Serial.print(apModeActive ? "ON" : "OFF");
        Serial.print(" | Deps=");
        Serial.print(departureCount);
        Serial.print(" | Heap=");
        Serial.println(ESP.getFreeHeap());
    }

    // Let idle task run
    delay(1);
}