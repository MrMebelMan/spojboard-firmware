#ifndef DISPLAYMANAGER_H
#define DISPLAYMANAGER_H

#include "../config/AppConfig.h"
#include "../api/DepartureData.h"
#include "DisplayColors.h"
#include <Adafruit_GFX.h>

// Platform-specific display includes
#if defined(MATRIX_PORTAL_M4)
    #include <Adafruit_Protomatter.h>
    typedef Adafruit_Protomatter DisplayType;
#else
    #include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
    typedef MatrixPanel_I2S_DMA DisplayType;
#endif

// Font references
extern const GFXfont DepartureMono_Regular4pt8b;
extern const GFXfont DepartureMono_Regular5pt8b;
extern const GFXfont DepartureMono_Condensed5pt8b;
extern const GFXfont DepartureWeather_Regular4pt8b;  // Weather icon font

// Forward declaration for WeatherData
struct WeatherData;

// ============================================================================
// Display Manager Class
// ============================================================================

class DisplayManager
{
public:
    DisplayManager();
    ~DisplayManager();

    /**
     * Initialize HUB75 display with pin configuration
     * @param brightness Initial brightness (0-255)
     * @return true if initialization succeeded
     */
    bool begin(int brightness = 90);

    /**
     * Set display brightness
     * @param brightness Brightness level (0-255)
     */
    void setBrightness(int brightness);

    /**
     * Update display with current state
     * @param departures Array of departures to display
     * @param departureCount Number of valid departures
     * @param numToDisplay Number of departures to show (1-3)
     * @param wifiConnected WiFi connection status
     * @param apModeActive AP mode status
     * @param apSSID AP network name (if in AP mode)
     * @param apPassword AP password (if in AP mode)
     * @param apiError API error status
     * @param apiErrorMsg API error message
     * @param stopName Current stop name
     * @param apiKeyConfigured Whether API key is configured
     * @param demoModeActive Whether demo mode is active (has highest priority, overrides ALL status screens)
     */
    void updateDisplay(const Departure* departures, int departureCount, int numToDisplay,
                      bool wifiConnected, bool apModeActive,
                      const char* apSSID, const char* apPassword,
                      bool apiError, const char* apiErrorMsg,
                      const char* stopName, bool apiKeyConfigured,
                      bool demoModeActive = false);

    /**
     * Draw status message (for temporary status during setup)
     * @param line1 First line of text
     * @param line2 Second line of text
     * @param color Text color
     */
    void drawStatus(const char* line1, const char* line2, uint16_t color);

    /**
     * Draw OTA firmware update progress
     * @param progress Bytes uploaded so far
     * @param total Total bytes to upload
     */
    void drawOTAProgress(size_t progress, size_t total);

    /**
     * Set configuration pointer for color mappings
     * @param cfg Pointer to Config struct
     */
    void setConfig(const Config* cfg) { config = cfg; }

    /**
     * Set weather data pointer for display rendering
     * @param data Pointer to WeatherData struct
     */
    void setWeatherData(const WeatherData* data) { weatherData = data; }

    /**
     * Get pointer to display object (for direct access if needed)
     */
    DisplayType* getDisplay() { return display; }

    /**
     * Get local IP address as string (platform-independent)
     */
    const char* getLocalIPString();

    /**
     * Draw demo mode display (repurposed from drawFontTest)
     * Shows sample departure data for customization testing
     * @param departures Array of sample departures to display
     * @param departureCount Number of departures (1-3)
     * @param stopName Stop name to display
     */
    void drawDemo(const Departure* departures, int departureCount, const char* stopName);

    /**
     * Turn screen off (fill black, pause updates)
     */
    void turnOff();

    /**
     * Turn screen on (resume updates)
     */
    void turnOn();

    bool isScreenOff() const { return screenOff; }
    bool needsRedraw() { bool r = forceRedraw; forceRedraw = false; return r; }

private:
    DisplayType* display;
    bool isDrawing;
    bool screenOff;
    bool forceRedraw;
    const Config* config;
    char ipStringBuffer[32];  // Buffer for IP string

    const GFXfont* fontSmall;
    const GFXfont* fontMedium;
    const GFXfont* fontCondensed;
    const GFXfont* fontWeather;  // Weather icon font

    // Weather data pointer
    const WeatherData* weatherData;

    // Drawing functions
    void drawDeparture(int row, const Departure& dep);
    void drawDateTime();
    void drawErrorBar(const char* errorMsg);
    void drawAPMode(const char* ssid, const char* password);

    // Weather helper functions
    char mapWeatherCodeToIcon(int wmoCode);
    uint16_t getWeatherColor(int wmoCode);
    uint16_t getTemperatureColor(int temperature);
};

#endif // DISPLAYMANAGER_H
