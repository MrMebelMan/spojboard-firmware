#include "DisplayManager.h"
#include "../utils/TimeUtils.h"
#include "../utils/gfxlatin2.h"
#include <Arduino.h>

// Platform-specific includes
#if defined(MATRIX_PORTAL_M4)
    #include <WiFiNINA.h>
#else
    #include <WiFi.h>
#endif

// Font references from src/fonts/ directory
#include "../fonts/DepartureMono4pt8b.h"
#include "../fonts/DepartureMono5pt8b.h"
#include "../fonts/DepartureMonoCondensed5pt8b.h"

DisplayManager::DisplayManager()
    : display(nullptr), isDrawing(false), screenOff(false), forceRedraw(false), config(nullptr)
{
    fontSmall = &DepartureMono_Regular4pt8b;
    fontMedium = &DepartureMono_Regular5pt8b;
    fontCondensed = &DepartureMono_Condensed5pt8b;
    ipStringBuffer[0] = '\0';
}

DisplayManager::~DisplayManager()
{
    if (display)
    {
        delete display;
        display = nullptr;
    }
}

bool DisplayManager::begin(int brightness)
{
#if defined(MATRIX_PORTAL_M4)
    // Matrix Portal M4 pin configuration for Protomatter
    // These are the default pins for the Matrix Portal M4 board
    uint8_t rgbPins[] = {7, 8, 9, 10, 11, 12};
    uint8_t addrPins[] = {17, 18, 19, 20};  // A, B, C, D (4 pins for 32-row 1:16 scan panels)
    uint8_t clockPin = 14;
    uint8_t latchPin = 15;
    uint8_t oePin = 16;

    display = new Adafruit_Protomatter(
        PANEL_WIDTH * PANELS_NUMBER,  // Total width (128)
        4,                             // Bit depth
        1,                             // Number of parallel chains
        rgbPins,
        4,                             // Number of address pins (4 for 32-row 1:16 scan)
        addrPins,
        clockPin,
        latchPin,
        oePin,
        true                           // Double-buffer
    );

    ProtomatterStatus status = display->begin();
    if (status != PROTOMATTER_OK)
    {
        Serial.print("Protomatter FAILED: ");
        Serial.println((int)status);
        return false;
    }

    display->fillScreen(0);
    display->show();

#else
    // ESP32-S3 pin configuration for I2S DMA
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
        return false;
    }

    display->setBrightness8(brightness);
    display->fillScreen(0);
#endif

    // Initialize color constants (platform-independent)
    initColors();

    return true;
}

void DisplayManager::setBrightness(int brightness)
{
    if (display)
    {
#if defined(MATRIX_PORTAL_M4)
        // Protomatter doesn't have runtime brightness control
        // Brightness is set via bit depth at initialization
        (void)brightness;  // Unused on M4
#else
        display->setBrightness8(brightness);
#endif
    }
}

void DisplayManager::turnOff()
{
    screenOff = true;
    if (display)
    {
        display->fillScreen(0);
#if defined(MATRIX_PORTAL_M4)
        display->show();
#else
        display->setBrightness8(0);
#endif
    }
}

void DisplayManager::turnOn()
{
    screenOff = false;
    forceRedraw = true;
#if !defined(MATRIX_PORTAL_M4)
    if (display && config)
    {
        display->setBrightness8(config->brightness);
    }
#endif
}

const char* DisplayManager::getLocalIPString()
{
    IPAddress ip = WiFi.localIP();
    sprintf(ipStringBuffer, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
    return ipStringBuffer;
}

void DisplayManager::drawDeparture(int row, const Departure &dep)
{
    int y = row * 8; // Each row is 8 pixels

    // Convert line number and destination to ISO-8859-2 (in-place)
    char lineConverted[8];
    char destConverted[32];
    strlcpy(lineConverted, dep.line, sizeof(lineConverted));
    strlcpy(destConverted, dep.destination, sizeof(destConverted));
    utf8tocp(lineConverted);
    utf8tocp(destConverted);

    // Draw line number background - always black (fixed width for all routes)
    uint16_t lineColor = getLineColorWithConfig(dep.line, config ? config->lineColorMap : "");
    int bgWidth = 18; // Fixed width to fit up to 3 characters
    display->fillRect(1, y + 1, bgWidth, 7, COLOR_BLACK);

    // Line number text - colored text on black background
    display->setTextColor(lineColor);
    display->setFont(fontMedium);

    // Center the line number text within the background rectangle
    int16_t x1, y1;
    uint16_t w, h;
    display->getTextBounds(lineConverted, 0, 0, &x1, &y1, &w, &h);
    // Account for font's left bearing offset (x1) when centering
    int textX = 1 + (bgWidth - w) / 2 - x1;
    // Align baseline with destination (y + 7)
    display->setCursor(textX, y + 7);
    display->print(lineConverted);

    // Direction indicator (R/L) before destination based on stop index
    int destX = 22; // Fixed position for all destinations (18px max route width + 4px gap)
    if (dep.stopIndex == 0) {
        display->setTextColor(COLOR_BLUE);
        display->setCursor(destX, y + 7);
        display->print("R");
        destX += 8;
    } else if (dep.stopIndex == 1) {
        display->setTextColor(COLOR_GREEN);
        display->setCursor(destX, y + 7);
        display->print("L");
        destX += 8;
    }

    // Destination - use condensed font for long names, always white
    display->setTextColor(COLOR_WHITE);

    int destLen = strlen(destConverted);
    int normalMaxChars = (dep.stopIndex <= 1) ? 14 : 15;
    const GFXfont* destFont;
    int maxChars;

    normalMaxChars -= (dep.eta >= 10 || dep.eta < 1) ? 1 : 0;

    if (destLen > normalMaxChars)
    {
        // Long destination - use condensed font
        destFont = fontCondensed;
        int condensedMax = (dep.eta >= 10 || dep.eta < 1) ? 22 : 23;
        maxChars = (dep.stopIndex <= 1) ? condensedMax - 1 : condensedMax;
    }
    else
    {
        // Short destination - use regular font
        destFont = fontMedium;
        maxChars = normalMaxChars;
    }

    display->setFont(destFont);
    display->setCursor(destX, y + 7);

    // Truncate destination if needed
    char destTrunc[24];
    strncpy(destTrunc, destConverted, maxChars);
    destTrunc[maxChars] = '\0';
    display->print(destTrunc);

    // ETA display
    int etaCursor = 117;
    if (dep.eta >= 10 || dep.eta < 1)
    {
        etaCursor = 111;
    }

    display->setFont(fontMedium);
    display->setCursor(etaCursor, y + 7);

    // ETA color based on time (red if <= 5 min, white otherwise)
    if (dep.eta <= 5)
    {
        display->setTextColor(COLOR_RED);
    }
    else
    {
        display->setTextColor(COLOR_WHITE);
    }

    if (dep.eta < 1)
    {
        display->print("<1'");
    }
    else if (dep.eta >= 60)
    {
        display->setCursor(etaCursor - 2, y + 7);
        display->print(">");
        display->setCursor(etaCursor + 6, y + 7);
        display->print("1");
        display->setFont(fontCondensed);
        display->print("h");
        display->setFont(fontMedium);
    }
    else
    {
        display->print(dep.eta);
        display->print("'");
    }
}

void DisplayManager::drawDateTime()
{
    int y = 24; // Bottom row

    struct tm timeinfo;
    if (!getCurrentTime(&timeinfo))
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
    char dayStr[6];
    strftime(dayStr, 6, "%a ", &timeinfo);
    utf8tocp(dayStr); // Convert Czech day names
    display->setCursor(2, y + 7);
    display->print(dayStr);

    // Date
    char dateStr[7];
    strftime(dateStr, 7, "%b %d", &timeinfo);
    utf8tocp(dateStr); // Convert Czech month names
    display->setCursor(21, y + 7);
    display->print(dateStr);

    // Time
    char timeStr[6];
    strftime(timeStr, 6, "%H:%M", &timeinfo);
    display->setCursor(102, y + 7);
    display->print(timeStr);
}

void DisplayManager::drawErrorBar(const char *errorMsg)
{
    int y = 24; // Bottom row
    display->fillRect(0, y, 128, 8, COLOR_BLACK); // Clear bottom row

    display->setFont(fontSmall);
    display->setTextColor(COLOR_RED);
    display->setCursor(2, y + 7);
    display->print("ERR: ");
    display->print(errorMsg);
}

void DisplayManager::drawStatus(const char *line1, const char *line2, uint16_t color)
{
    display->fillScreen(0);
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

#if defined(MATRIX_PORTAL_M4)
    display->show();
#endif
}

void DisplayManager::drawOTAProgress(size_t progress, size_t total)
{
    if (isDrawing)
        return;

    isDrawing = true;

    display->fillScreen(0);

    // Title
    display->setFont(fontMedium);
    display->setTextColor(COLOR_CYAN);
    display->setCursor(2, 8);
    display->print("Uploading...");

    // Calculate percentage
    int percentage = 0;
    if (total > 0)
    {
        percentage = (progress * 100) / total;
        if (percentage > 100)
            percentage = 100;
    }

    // Draw progress bar (center of display)
    int barWidth = 120; // Total bar width
    int barHeight = 10;
    int barX = 4;  // 4px left margin
    int barY = 13; // Center vertically

    // Draw border
    display->drawRect(barX, barY, barWidth, barHeight, COLOR_WHITE);

    // Fill progress
    int fillWidth = ((barWidth - 2) * percentage) / 100;
    if (fillWidth > 0)
    {
        display->fillRect(barX + 1, barY + 1, fillWidth, barHeight - 2, COLOR_CYAN);
    }

    // Display percentage text
    display->setFont(fontMedium);
    display->setTextColor(COLOR_WHITE);
    char percentStr[8];
    sprintf(percentStr, "%d%%", percentage);

    // Center the percentage text at the bottom
    int16_t x1, y1;
    uint16_t w, h;
    display->getTextBounds(percentStr, 0, 0, &x1, &y1, &w, &h);
    int textX = (128 - w) / 2 - x1;

    display->setCursor(textX, 31);
    display->print(percentStr);

#if defined(MATRIX_PORTAL_M4)
    display->show();
#endif

    isDrawing = false;
}

void DisplayManager::drawAPMode(const char *ssid, const char *password)
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
    display->print(ssid);

    // Password
    display->setTextColor(COLOR_WHITE);
    display->setCursor(2, 23);
    display->print("Pass:");
    display->setTextColor(COLOR_GREEN);
    display->setCursor(32, 23);
    display->print(password);

    // IP
    display->setTextColor(COLOR_WHITE);
    display->setCursor(2, 31);
    display->print("Go to: 192.168.4.1");
}

void DisplayManager::updateDisplay(const Departure *departures, int departureCount, int numToDisplay,
                                   bool wifiConnected, bool apModeActive,
                                   const char *apSSID, const char *apPassword,
                                   bool apiError, const char *apiErrorMsg,
                                   const char *stopName, bool apiKeyConfigured,
                                   bool demoModeActive)
{
    if (isDrawing)
        return;

    // Skip all drawing if screen is off
    if (screenOff)
        return;

    isDrawing = true;
    display->fillScreen(0);
    delay(1);

    // Demo mode has highest priority - bypass all status screens
    // and show demo departures regardless of WiFi/API/config state
    if (demoModeActive)
    {
        // Draw demo departures directly
        int rowsToDraw = (departureCount < numToDisplay) ? departureCount : numToDisplay;
        if (rowsToDraw > 3)
            rowsToDraw = 3; // Maximum 3 rows on display

        for (int i = 0; i < rowsToDraw; i++)
        {
            drawDeparture(i, departures[i]);
            delay(1);
        }

        drawDateTime();
        delay(1);

#if defined(MATRIX_PORTAL_M4)
        display->show();
#endif

        isDrawing = false;
        return;
    }

    // AP Mode - Show credentials
    if (apModeActive)
    {
        drawAPMode(apSSID, apPassword);
#if defined(MATRIX_PORTAL_M4)
        display->show();
#endif
        isDrawing = false;
        return;
    }

    if (!wifiConnected)
    {
        // We don't have access to config here, so just show generic message
        drawStatus("WiFi Connecting...", "", COLOR_YELLOW);
        isDrawing = false;
        return;
    }

    if (!apiKeyConfigured)
    {
        char ipStr[32];
        sprintf(ipStr, "http://%s", getLocalIPString());
        drawStatus("Setup Required", ipStr, COLOR_CYAN);
        isDrawing = false;
        return;
    }

    if (departureCount == 0 && !apiError)
    {
        drawStatus("No Departures", stopName[0] ? stopName : "Waiting...", COLOR_YELLOW);
        drawDateTime();
        isDrawing = false;
        return;
    }

    // Draw departures (top 3 rows, or fewer if numToDisplay is less)
    int rowsToDraw = (departureCount < numToDisplay) ? departureCount : numToDisplay;
    if (rowsToDraw > 3)
        rowsToDraw = 3; // Maximum 3 rows on display

    for (int i = 0; i < rowsToDraw; i++)
    {
        drawDeparture(i, departures[i]);
        delay(1);
    }

    // Show error in status bar if API error, otherwise show date/time
    if (apiError)
    {
        drawErrorBar(apiErrorMsg);
    }
    else
    {
        drawDateTime();
    }
    delay(1);

#if defined(MATRIX_PORTAL_M4)
    display->show();
#endif

    isDrawing = false;
}

void DisplayManager::drawDemo(const Departure* departures, int departureCount, const char* stopName)
{
    if (isDrawing)
        return;

    isDrawing = true;
    display->fillScreen(0);
    delay(1);

    // Draw sample departures (top 1-3 rows)
    int rowsToDraw = (departureCount < 3) ? departureCount : 3;
    for (int i = 0; i < rowsToDraw; i++)
    {
        drawDeparture(i, departures[i]);
        delay(1);
    }

    // Draw date/time status bar
    drawDateTime();
    delay(1);

#if defined(MATRIX_PORTAL_M4)
    display->show();
#endif

    isDrawing = false;
}
