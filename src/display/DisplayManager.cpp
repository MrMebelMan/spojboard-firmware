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
    display->getTextBounds(dep.line, 0, 0, &x1, &y1, &w, &h);
    // Account for font's left bearing offset (x1) when centering
    int textX = 1 + (bgWidth - w) / 2 - x1;
    // Align baseline with destination (y + 7)
    display->setCursor(textX, y + 7);
    display->print(dep.line);

    // AC indicator (asterisk before destination)
    int destX = 22; // Fixed position for all destinations (18px max route width + 4px gap)
    if (dep.hasAC)
    {
        display->setTextColor(COLOR_CYAN);
        display->setCursor(destX, y + 7);
        display->print("*");
        destX += 6;
    }

    // Destination - use condensed font for long names
    display->setTextColor(COLOR_WHITE);

    int destLen = strlen(dep.destination);
    int normalMaxChars = dep.hasAC ? 14 : 15;
    const GFXfont* destFont;
    int maxChars;
    bool useCondensed = false;

    if (destLen > normalMaxChars)
    {
        // Long destination - use condensed font
        destFont = fontCondensed;
        maxChars = 23;
        useCondensed = true;
    }
    else
    {
        // Short destination - use regular font
        destFont = fontMedium;
        maxChars = normalMaxChars;
        useCondensed = false;
    }

    // Adjust for ETA width (2-digit and 3-digit numbers take more space)
    // Condensed font needs more reduction due to narrower characters
    if (useCondensed)
    {
        maxChars -= (dep.eta >= 100 ? 3 : (dep.eta >= 10 ? 2 : 1));
    }
    else
    {
        maxChars -= (dep.eta >= 100 ? 2 : (dep.eta >= 10 ? 1 : 0));
    }

    display->setFont(destFont);
    display->setCursor(destX, y + 7);

    // Truncate destination if needed
    char destTrunc[24];
    strncpy(destTrunc, dep.destination, maxChars);
    destTrunc[maxChars] = '\0';
    display->print(destTrunc);

    // ETA display
    int etaCursor = 111;
    if (dep.eta >= 100)
    {
        etaCursor = 105;
    }
    else if (dep.eta >= 10 || dep.eta < 1)
    {
        etaCursor = 111;
    }
    else
    {
        etaCursor = 117;
    }

    display->setFont(fontMedium);
    display->setCursor(etaCursor, y + 7);

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
        display->print(">1h");
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
    strftime(dayStr, 6, "%a|", &timeinfo);
    utf8tocp(dayStr); // Convert Czech day names
    display->setCursor(2, y + 7);
    display->print(dayStr);

    // Date
    char dateStr[7];
    strftime(dateStr, 7, "%b %d", &timeinfo);
    utf8tocp(dateStr); // Convert Czech month names
    display->setCursor(19, y + 7);
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
                                   const char *stopName, bool apiKeyConfigured)
{
    if (isDrawing)
        return;

    isDrawing = true;
    display->clearScreen();
    delay(1);

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

void DisplayManager::drawFontTest()
{
    if (isDrawing)
        return;

    isDrawing = true;
    display->clearScreen();

    // Czech special characters test string (UTF-8)
    char testString[64];
    strcpy(testString, "ĚŠČŘŽÝÁÍÉÚŮĎŤŇ");

    // Convert UTF-8 to ISO-8859-2 for display
    utf8tocp(testString);

    // Row 0: Small font (4pt)
    display->setFont(fontSmall);
    display->setTextColor(COLOR_CYAN);
    display->setCursor(2, 7);
    display->print("Small:");
    display->setTextColor(COLOR_WHITE);
    display->setCursor(35, 7);
    display->print(testString);

    // Row 1: Medium font (5pt)
    display->setFont(fontMedium);
    display->setTextColor(COLOR_GREEN);
    display->setCursor(2, 15);
    display->print("Med:");
    display->setTextColor(COLOR_WHITE);
    display->setCursor(28, 15);
    display->print(testString);

    // Row 2: Condensed font (5pt)
    display->setFont(fontCondensed);
    display->setTextColor(COLOR_YELLOW);
    display->setCursor(2, 23);
    display->print("Cond:");
    display->setTextColor(COLOR_WHITE);
    display->setCursor(32, 23);
    display->print(testString);

    // Row 3: Status
    display->setFont(fontSmall);
    display->setTextColor(COLOR_PURPLE);
    display->setCursor(2, 31);
    display->print("Czech char test OK");

    isDrawing = false;
}
