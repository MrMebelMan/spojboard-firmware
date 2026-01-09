#include "AppConfig.h"
#include "../utils/Logger.h"
#include <Arduino.h>

void loadConfig(Config& config)
{
    Preferences preferences;
    preferences.begin("transport", true); // Read-only

    strlcpy(config.wifiSsid, preferences.getString("wifiSsid", DEFAULT_WIFI_SSID).c_str(), sizeof(config.wifiSsid));
    strlcpy(config.wifiPassword, preferences.getString("wifiPass", DEFAULT_WIFI_PASSWORD).c_str(), sizeof(config.wifiPassword));
    strlcpy(config.apiKey, preferences.getString("apiKey", "").c_str(), sizeof(config.apiKey));
    strlcpy(config.stopIds, preferences.getString("stopIds", "U693Z2P").c_str(), sizeof(config.stopIds));
    config.refreshInterval = preferences.getInt("refresh", 30);
    config.numDepartures = preferences.getInt("numDeps", 3);
    config.minDepartureTime = preferences.getInt("minDepTime", 3);
    config.brightness = preferences.getInt("brightness", 90);
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

void saveConfig(const Config& config)
{
    Preferences preferences;
    preferences.begin("transport", false); // Read-write

    preferences.putString("wifiSsid", config.wifiSsid);
    preferences.putString("wifiPass", config.wifiPassword);
    preferences.putString("apiKey", config.apiKey);
    preferences.putString("stopIds", config.stopIds);
    preferences.putInt("refresh", config.refreshInterval);
    preferences.putInt("numDeps", config.numDepartures);
    preferences.putInt("minDepTime", config.minDepartureTime);
    preferences.putInt("brightness", config.brightness);
    preferences.putBool("configured", true);

    preferences.end();

    logTimestamp();
    Serial.println("Config saved");
}
