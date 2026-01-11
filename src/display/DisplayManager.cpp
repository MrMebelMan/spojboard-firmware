#include "DisplayManager.h"
#include "../utils/TimeUtils.h"
#include "../utils/gfxlatin2.h"
#include <WiFi.h>
#include <Arduino.h>

// Font references from src/fonts/ directory
#include "../fonts/DepartureMono4pt8b.h"
#include "../fonts/DepartureMono5pt8b.h"
#include "../fonts/DepartureMonoCondensed5pt8b.h"

DisplayManager::DisplayManager()
    : display(nullptr), isDrawing(false), config(nullptr)
{
    fontSmall = &DepartureMono_Regular4pt8b;
    fontMedium = &DepartureMono_Regular5pt8b;
    fontCondensed = &DepartureMono_Condensed5pt8b;
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
    display->clearScreen();

    // Initialize color constants
    initColors(display);

    return true;
}

void DisplayManager::setBrightness(int brightness)
{
    if (display)
    {
        display->setBrightness8(brightness);
    }
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
    int bgWidth = 18; // Fixed width to fit up to 4 characters
    display->fillRect(1, y + 1, bgWidth, 7, COLOR_BLACK);

    // Line number text - colored text on black background
    display->setTextColor(lineColor);

    // Select font based on line number length
    // 1-3 characters: medium font (6px/char)
    // 4 characters: condensed font (4px/char)
    int lineLen = strlen(lineConverted);
    const GFXfont* lineFont = (lineLen >= 4) ? fontCondensed : fontMedium;
    display->setFont(lineFont);

    // Center the line number text within the background rectangle
    int16_t x1, y1;
    uint16_t w, h;
    display->getTextBounds(lineConverted, 0, 0, &x1, &y1, &w, &h);
    // Account for font's left bearing offset (x1) when centering
    int textX = 1 + (bgWidth - w) / 2 - x1;
    // Align baseline with destination (y + 7)
    display->setCursor(textX, y + 7);
    display->print(lineConverted);

    // AC indicator (asterisk before destination)
    int destX = 22; // Fixed position for all destinations (18px max route width + 4px gap)
    if (dep.hasAC)
    {
        display->setTextColor(COLOR_CYAN);
        display->setCursor(destX, y + 7);
        display->print("*");
        destX += 6;
    }

    // Calculate space reserved for platform (if enabled and present)
    int platformReservedPx = 0;
    bool willShowPlatform = false;
    if (config && config->showPlatform && dep.platform[0] != '\0')
    {
        willShowPlatform = true;
        // Dynamic reservation based on platform length:
        // 1 char: medium font (6px) + 3px buffer = 9px
        // 2 chars: condensed font (8px) + 3px buffer = 11px
        // 3 chars: condensed font (12px) + 3px buffer = 15px
        int platformLen = strlen(dep.platform);
        if (platformLen >= 3)
        {
            platformReservedPx = 15;  // 3 condensed chars + buffer
        }
        else if (platformLen == 2)
        {
            platformReservedPx = 11;  // 2 condensed chars + buffer
        }
        else
        {
            platformReservedPx = 9;   // 1 medium char + buffer
        }
    }

    // Destination - adaptive font based on available space
    display->setTextColor(COLOR_WHITE);

    int destLen = strlen(destConverted);

    // Calculate available space: ETA position - destination start - platform reservation
    // ETA positions: 111px (wide: >=10 or <1), 117px (narrow: 1-9)
    int etaPosition = (dep.eta >= 10 || dep.eta < 1) ? 111 : 117;

    // When platform is shown, always reserve space for widest ETA (111px)
    // to keep platform alignment consistent across rows
    int spaceCalcEta = (platformReservedPx > 0) ? 111 : etaPosition;
    int availableSpace = spaceCalcEta - destX - platformReservedPx;

    // Font selection based on destination length and available space
    const GFXfont* destFont;
    int maxChars;

    // Thresholds: fontMedium @ 6px/char, fontCondensed @ 4px/char
    // Without platform: ~89px → 14 chars (medium) or 22 chars (condensed)
    // With platform: ~74px → 12 chars (medium) or 18 chars (condensed)
    int mediumThreshold = platformReservedPx > 0 ? 12 : 14;
    mediumThreshold -= dep.hasAC ? 1 : 0;

    if (destLen <= mediumThreshold)
    {
        // Short destination - use regular font
        destFont = fontMedium;
        maxChars = availableSpace / 6;  // 6px per char
    }
    else
    {
        // Long destination - use condensed font
        destFont = fontCondensed;
        maxChars = availableSpace / 4;  // 4px per char
    }

    // Safety cap: prevent buffer overflow
    if (maxChars > 23) maxChars = 23;  // destTrunc buffer size
    if (maxChars < 1) maxChars = 1;     // Ensure at least 1 char

    display->setFont(destFont);
    display->setCursor(destX, y + 7);

    // Truncate destination if needed
    char destTrunc[24];
    strncpy(destTrunc, destConverted, maxChars);
    destTrunc[maxChars] = '\0';
    display->print(destTrunc);

    // Platform display (if enabled and present)
    if (willShowPlatform)
    {
        // Convert platform to ISO-8859-2 (in case of special characters)
        char platformConverted[8];
        strlcpy(platformConverted, dep.platform, sizeof(platformConverted));
        utf8tocp(platformConverted);

        // Safety truncation to 3 characters
        if (strlen(platformConverted) > 3)
        {
            platformConverted[3] = '\0';
        }

        // Select font based on platform length (same logic as line numbers)
        int platformLen = strlen(platformConverted);
        const GFXfont* platformFont = (platformLen >= 2) ? fontCondensed : fontMedium;
        display->setFont(platformFont);

        // Get actual text bounds for proper alignment (like line number centering)
        int16_t px1, py1;
        uint16_t pw, ph;
        display->getTextBounds(platformConverted, 0, 0, &px1, &py1, &pw, &ph);

        // Position: right-align to widest ETA position (111px for ">1h")
        // Account for font's left bearing offset (px1) for precise positioning
        int platformX = 111 - pw - 3 - px1;

        display->setTextColor(COLOR_CYAN);  // Match AC indicator
        display->setCursor(platformX, y + 7);
        display->print(platformConverted);
    }

    // ETA display (use etaPosition already calculated above)
    display->setFont(fontMedium);
    display->setCursor(etaPosition, y + 7);

    // ETA color based on time
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
        display->setCursor(etaPosition - 2, y + 7);
        display->print(">");
        display->setCursor(etaPosition + 6, y + 7);
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

    // Get language setting (default to "en" if config not set)
    const char* lang = (config && config->language[0]) ? config->language : "en";

    // Day of week (localized)
    char dayStr[8];
    const char* localDay = getLocalizedDay(timeinfo.tm_wday, lang);
    snprintf(dayStr, sizeof(dayStr), "%s", localDay);
    utf8tocp(dayStr);
    display->setCursor(2, y + 7);
    display->print(dayStr);

    // Date (localized month)
    char dateStr[10];
    const char* localMonth = getLocalizedMonth(timeinfo.tm_mon, lang);
    snprintf(dateStr, sizeof(dateStr), "%s %02d", localMonth, timeinfo.tm_mday);
    utf8tocp(dateStr);
    display->setCursor(26, y + 7);
    display->print(dateStr);

    // Time
    char timeStr[6];
    strftime(timeStr, 6, "%H:%M", &timeinfo);
    display->setCursor(102, y + 7);
    display->print(timeStr);
}

void DisplayManager::drawStatus(const char *line1, const char *line2, uint16_t color)
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

void DisplayManager::drawOTAProgress(size_t progress, size_t total)
{
    if (isDrawing)
        return;

    isDrawing = true;

    display->clearScreen();

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

    isDrawing = true;
    display->clearScreen();
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

        isDrawing = false;
        return;
    }

    // AP Mode - Show credentials
    if (apModeActive)
    {
        drawAPMode(apSSID, apPassword);
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

    // Draw departures (top 3 rows, or fewer if numToDisplay is less)
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

    isDrawing = false;
}

void DisplayManager::drawDemo(const Departure* departures, int departureCount, const char* stopName)
{
    if (isDrawing)
        return;

    isDrawing = true;
    display->clearScreen();
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

    isDrawing = false;
}
