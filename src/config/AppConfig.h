#ifndef APPCONFIG_H
#define APPCONFIG_H

#include <Preferences.h>
#include <cstdint>

// ============================================================================
// Firmware Version
// ============================================================================
#define FIRMWARE_RELEASE "1"

// Build ID is injected by build script (8 hex characters)
// Generated from build timestamp using DJB2 hash algorithm
#ifndef BUILD_ID
#define BUILD_ID 0x00000000  // Fallback if not set by build system
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

// Default WiFi credentials (for initial setup)
#define DEFAULT_WIFI_SSID "Your WiFi SSID"
#define DEFAULT_WIFI_PASSWORD "Your WiFi Password"

// ============================================================================
// Configuration Structure
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
    int brightness;       // Display brightness (0-255)
    bool configured;
};

// ============================================================================
// Configuration Management Functions
// ============================================================================

/**
 * Load configuration from NVS flash storage
 * @param config Reference to Config structure to populate
 */
void loadConfig(Config& config);

/**
 * Save configuration to NVS flash storage
 * @param config Configuration to save
 */
void saveConfig(const Config& config);

#endif // APPCONFIG_H
