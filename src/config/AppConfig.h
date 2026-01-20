#ifndef APPCONFIG_H
#define APPCONFIG_H

#include <cstdint>

// Note: Platform-specific storage includes (FlashStorage_SAMD, Preferences)
// are in AppConfig.cpp only to avoid multiple definition errors

// ============================================================================
// Firmware Version
// ============================================================================
#define FIRMWARE_RELEASE "3"

// Build ID is injected by build script (8 hex characters)
// Generated from build timestamp using DJB2 hash algorithm
#ifndef BUILD_ID
#define BUILD_ID 0x00000000 // Fallback if not set by build system
#endif

// ============================================================================
// GitHub OTA Configuration
// ============================================================================
#define GITHUB_REPO_OWNER "xbach"
#define GITHUB_REPO_NAME "spojboard-firmware"

// ============================================================================
// Hardware Configuration (HUB75 Display)
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

// Private credentials (WiFi, API keys, stop IDs)
// Copy credentials.h.example to credentials.h and fill in your values
#include "credentials.h"

// ============================================================================
// Configuration Structure
// ============================================================================

struct Config
{
    char wifiSsid[64];
    char wifiPassword[64];

    // Per-city configuration fields
    char pragueApiKey[300];    // Golemio API key for Prague
    char pragueStopIds[128];   // Prague stop IDs (e.g., "U693Z2P,U693Z1P")
    char berlinStopIds[128];   // Berlin stop IDs (e.g., "900013102")
    // Note: Berlin BVG API requires no authentication

    int refreshInterval;    // Seconds between API calls
    int numDepartures;      // Number of departures to display (1-3 rows on LED matrix)
    int minDepartureTime;   // Minimum departure time in minutes (filter out departures < this)
    int brightness;         // Display brightness (0-255)
    char lineColorMap[256]; // Line color mappings (format: "A=GREEN,B=YELLOW,9*=CYAN")
    char city[16];          // Transit city: "Prague" or "Berlin"
    bool debugMode;         // Enable telnet logging and verbose output
    bool noApFallback;      // If true, keep retrying WiFi instead of falling back to AP mode

    // Weather configuration
    bool weatherEnabled;        // Enable weather display
    float weatherLatitude;      // GPS latitude (e.g., 50.0755 for Prague)
    float weatherLongitude;     // GPS longitude (e.g., 14.4378 for Prague)
    int weatherRefreshInterval; // Minutes between weather fetches (default: 15)

    bool configured;
};

// ============================================================================
// Configuration Management Functions
// ============================================================================

/**
 * Load configuration from NVS flash storage
 * @param config Reference to Config structure to populate
 */
void loadConfig(Config &config);

/**
 * Save configuration to NVS flash storage
 * @param config Configuration to save
 */
void saveConfig(const Config &config);

/**
 * Clear all configuration from NVS flash storage
 * Resets device to factory defaults - will boot into AP mode on next restart
 */
void clearConfig();

#endif // APPCONFIG_H
