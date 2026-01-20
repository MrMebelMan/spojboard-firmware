#include "AppConfig.h"
#include "../utils/Logger.h"
#include <Arduino.h>

// Platform-specific storage includes (must be in .cpp only to avoid multiple definition)
#if defined(MATRIX_PORTAL_M4)
#include <FlashStorage_SAMD.h>
#else
#include <Preferences.h>
#endif

#if defined(MATRIX_PORTAL_M4)
// ============================================================================
// M4 Storage Implementation (FlashStorage_SAMD)
// ============================================================================

// Storage signature for validation
// Bump this value to invalidate stored configs and force fresh defaults
#define CONFIG_SIGNATURE 0x53504F4B  // Bumped to add weather fields

struct StoredConfig {
    uint32_t signature;
    Config config;
};

// Declare flash storage - must be at file scope
FlashStorage(config_storage, StoredConfig);

void loadConfig(Config& config)
{
    StoredConfig stored;
    config_storage.read(stored);

    // Check if storage has valid data
    if (stored.signature != CONFIG_SIGNATURE)
    {
        // Initialize with defaults
        memset(&config, 0, sizeof(Config));
        strlcpy(config.wifiSsid, DEFAULT_WIFI_SSID, sizeof(config.wifiSsid));
        strlcpy(config.wifiPassword, DEFAULT_WIFI_PASSWORD, sizeof(config.wifiPassword));
        strlcpy(config.pragueApiKey, DEFAULT_GOLEMIO_API_KEY, sizeof(config.pragueApiKey));
        strlcpy(config.pragueStopIds, DEFAULT_PRAGUE_STOP_IDS, sizeof(config.pragueStopIds));
        strlcpy(config.city, "Prague", sizeof(config.city));
        config.refreshInterval = 300;
        config.numDepartures = 3;
        config.minDepartureTime = 3;
        config.brightness = 45;
        config.debugMode = false;
        config.noApFallback = true;
        // Weather defaults - enabled by default on M4 since there's no web UI
        config.weatherEnabled = true;
        config.weatherLatitude = DEFAULT_WEATHER_LATITUDE;
        config.weatherLongitude = DEFAULT_WEATHER_LONGITUDE;
        config.weatherRefreshInterval = 15;
        config.configured = true;  // Mark as configured since we have hardcoded credentials

        Serial.println("Config: No valid data found, using defaults");
    }
    else
    {
        // Copy stored config
        memcpy(&config, &stored.config, sizeof(Config));

        // Migration: Initialize weather fields if they appear unset (added in later firmware)
        // Weather latitude/longitude of 0,0 is in the ocean, so treat as uninitialized
        if (config.weatherLatitude == 0.0f && config.weatherLongitude == 0.0f)
        {
            config.weatherEnabled = true;  // Enable by default on M4
            config.weatherLatitude = DEFAULT_WEATHER_LATITUDE;
            config.weatherLongitude = DEFAULT_WEATHER_LONGITUDE;
            config.weatherRefreshInterval = 15;
            Serial.println("Config: Migrated weather settings from defaults");
        }
    }

    // Log loaded config
    logTimestamp();
    Serial.println("Config loaded:");
    Serial.print("  SSID: ");
    Serial.println(config.wifiSsid);
    Serial.print("  City: ");
    Serial.println(config.city);
    Serial.print("  Prague API Key: ");
    Serial.println(strlen(config.pragueApiKey) > 0 ? "Configured" : "Not set");
    Serial.print("  Prague Stops: ");
    Serial.println(config.pragueStopIds);
    Serial.print("  Berlin Stops: ");
    Serial.println(strlen(config.berlinStopIds) > 0 ? config.berlinStopIds : "Not set");
    Serial.print("  Refresh: ");
    Serial.print(config.refreshInterval);
    Serial.println("s");
    Serial.print("  Weather: ");
    Serial.print(config.weatherEnabled ? "Enabled" : "Disabled");
    Serial.print(" (");
    Serial.print(config.weatherLatitude, 4);
    Serial.print(", ");
    Serial.print(config.weatherLongitude, 4);
    Serial.println(")");
    Serial.print("  Configured: ");
    Serial.println(config.configured ? "Yes" : "No");
}

void saveConfig(const Config& config)
{
    StoredConfig stored;
    stored.signature = CONFIG_SIGNATURE;
    memcpy(&stored.config, &config, sizeof(Config));

    // Mark as configured
    stored.config.configured = true;

    config_storage.write(stored);

    logTimestamp();
    debugPrintln("Config saved");
}

void clearConfig()
{
    StoredConfig stored;
    stored.signature = 0;  // Invalid signature = cleared
    memset(&stored.config, 0, sizeof(Config));

    config_storage.write(stored);

    logTimestamp();
    debugPrintln("All configuration cleared - device will boot into AP mode on restart");
}

#else
// ============================================================================
// ESP32 Storage Implementation (NVS Preferences)
// ============================================================================

void loadConfig(Config& config)
{
    Preferences preferences;
    preferences.begin("transport", true); // Read-only

    strlcpy(config.wifiSsid, preferences.getString("wifiSsid", DEFAULT_WIFI_SSID).c_str(), sizeof(config.wifiSsid));
    strlcpy(config.wifiPassword, preferences.getString("wifiPass", DEFAULT_WIFI_PASSWORD).c_str(), sizeof(config.wifiPassword));

    // Load per-city configuration fields
    strlcpy(config.pragueApiKey, preferences.getString("pragueApiKey", "").c_str(), sizeof(config.pragueApiKey));
    strlcpy(config.pragueStopIds, preferences.getString("pragueStopIds", DEFAULT_PRAGUE_STOP_IDS).c_str(), sizeof(config.pragueStopIds));
    strlcpy(config.berlinStopIds, preferences.getString("berlinStopIds", "").c_str(), sizeof(config.berlinStopIds));

    // Backward compatibility: Migrate old config format to new per-city fields
    // If old fields exist and new fields are empty, migrate the data
    if (preferences.isKey("apiKey") && strlen(config.pragueApiKey) == 0)
    {
        String oldApiKey = preferences.getString("apiKey", "");
        if (oldApiKey.length() > 0)
        {
            strlcpy(config.pragueApiKey, oldApiKey.c_str(), sizeof(config.pragueApiKey));
            Serial.println("  Migrated old apiKey to pragueApiKey");
        }
    }
    if (preferences.isKey("stopIds"))
    {
        String oldStopIds = preferences.getString("stopIds", "");
        if (oldStopIds.length() > 0)
        {
            // Migrate to Prague by default (backward compatibility)
            if (strlen(config.pragueStopIds) == 0 || strcmp(config.pragueStopIds, "U693Z2P") == 0)
            {
                strlcpy(config.pragueStopIds, oldStopIds.c_str(), sizeof(config.pragueStopIds));
                Serial.println("  Migrated old stopIds to pragueStopIds");
            }
        }
    }

    config.refreshInterval = preferences.getInt("refresh", 300);
    config.numDepartures = preferences.getInt("numDeps", 3);     // Display rows (1-3)
    config.minDepartureTime = preferences.getInt("minDepTime", 3);
    config.brightness = preferences.getInt("brightness", 90);
    strlcpy(config.lineColorMap, preferences.getString("lineColorMap", "").c_str(), sizeof(config.lineColorMap));
    strlcpy(config.city, preferences.getString("city", "Prague").c_str(), sizeof(config.city));  // Default: Prague for backward compatibility
    config.debugMode = preferences.getBool("debugMode", false);  // Default: disabled
    config.noApFallback = preferences.getBool("noApFallback", true);  // Default: keep retrying WiFi

    // Load weather configuration
    config.weatherEnabled = preferences.getBool("weatherEnable", false);  // Default: disabled
    config.weatherLatitude = preferences.getFloat("weatherLat", DEFAULT_WEATHER_LATITUDE);
    config.weatherLongitude = preferences.getFloat("weatherLon", DEFAULT_WEATHER_LONGITUDE);
    config.weatherRefreshInterval = preferences.getInt("weatherRefresh", 15);  // Default: 15 minutes

    config.configured = preferences.getBool("configured", false);

    preferences.end();

    // Config loading happens before logger init, so use Serial directly
    logTimestamp();
    Serial.println("Config loaded:");
    Serial.print("  SSID: ");
    Serial.println(config.wifiSsid);
    Serial.print("  City: ");
    Serial.println(config.city);
    Serial.print("  Prague API Key: ");
    Serial.println(strlen(config.pragueApiKey) > 0 ? "Configured" : "Not set");
    Serial.print("  Prague Stops: ");
    Serial.println(config.pragueStopIds);
    Serial.print("  Berlin Stops: ");
    Serial.println(strlen(config.berlinStopIds) > 0 ? config.berlinStopIds : "Not set");
    Serial.print("  Refresh: ");
    Serial.print(config.refreshInterval);
    Serial.println("s");
    Serial.print("  Configured: ");
    Serial.println(config.configured ? "Yes" : "No");
}

void saveConfig(const Config& config)
{
    Preferences preferences;
    preferences.begin("transport", false); // Read-write

    preferences.putString("wifiSsid", config.wifiSsid);
    preferences.putString("wifiPass", config.wifiPassword);

    // Save per-city configuration fields
    preferences.putString("pragueApiKey", config.pragueApiKey);
    preferences.putString("pragueStopIds", config.pragueStopIds);
    preferences.putString("berlinStopIds", config.berlinStopIds);

    // Remove old keys if they exist (cleanup after migration)
    if (preferences.isKey("apiKey"))
    {
        preferences.remove("apiKey");
    }
    if (preferences.isKey("stopIds"))
    {
        preferences.remove("stopIds");
    }

    preferences.putInt("refresh", config.refreshInterval);
    preferences.putInt("numDeps", config.numDepartures);
    preferences.putInt("minDepTime", config.minDepartureTime);
    preferences.putInt("brightness", config.brightness);
    preferences.putString("lineColorMap", config.lineColorMap);
    preferences.putString("city", config.city);
    preferences.putBool("debugMode", config.debugMode);
    preferences.putBool("noApFallback", config.noApFallback);

    // Save weather configuration
    preferences.putBool("weatherEnable", config.weatherEnabled);
    preferences.putFloat("weatherLat", config.weatherLatitude);
    preferences.putFloat("weatherLon", config.weatherLongitude);
    preferences.putInt("weatherRefresh", config.weatherRefreshInterval);

    preferences.putBool("configured", true);

    preferences.end();

    logTimestamp();
    debugPrintln("Config saved");
}

void clearConfig()
{
    Preferences preferences;
    preferences.begin("transport", false); // Read-write

    // Clear all keys in the namespace
    preferences.clear();

    preferences.end();

    logTimestamp();
    debugPrintln("All configuration cleared - device will boot into AP mode on restart");
}

#endif // MATRIX_PORTAL_M4
