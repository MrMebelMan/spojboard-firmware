#include "WiFiManager.h"
#include "../utils/Logger.h"
#include "../display/DisplayColors.h"
#include <esp_random.h>

WiFiManager::WiFiManager()
    : apModeActive(false)
{
    apSSID[0] = '\0';
    apPassword[0] = '\0';
}

bool WiFiManager::connectSTA(const Config& config, int maxAttempts, int delayMs)
{
    logTimestamp();
    Serial.print("WiFi: Connecting to ");
    Serial.println(config.wifiSsid);

    WiFi.mode(WIFI_STA);
    WiFi.begin(config.wifiSsid, config.wifiPassword);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < maxAttempts)
    {
        delay(delayMs);
        Serial.print(".");
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED)
    {
        logTimestamp();
        Serial.println("\nWiFi: Connected!");
        Serial.print("IP: ");
        Serial.println(WiFi.localIP());
        return true;
    }
    else
    {
        logTimestamp();
        Serial.println("\nWiFi: Connection failed!");
        return false;
    }
}

bool WiFiManager::startAP()
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
    if (!WiFi.softAP(apSSID, apPassword))
    {
        logTimestamp();
        Serial.println("AP Mode failed to start!");
        return false;
    }

    // Configure AP IP (default is 192.168.4.1)
    IPAddress apIP(192, 168, 4, 1);
    IPAddress gateway(192, 168, 4, 1);
    IPAddress subnet(255, 255, 255, 0);
    WiFi.softAPConfig(apIP, gateway, subnet);

    apModeActive = true;

    logTimestamp();
    Serial.println("AP Mode Active!");
    Serial.print("  SSID: ");
    Serial.println(apSSID);
    Serial.print("  Password: ");
    Serial.println(apPassword);
    Serial.print("  IP: ");
    Serial.println(WiFi.softAPIP());

    return true;
}

void WiFiManager::stopAP()
{
    if (apModeActive)
    {
        logTimestamp();
        Serial.println("Stopping AP Mode...");

        WiFi.softAPdisconnect(true);
        apModeActive = false;

        delay(100);
    }
}

bool WiFiManager::isConnected() const
{
    return WiFi.status() == WL_CONNECTED;
}

IPAddress WiFiManager::getAPIP() const
{
    return WiFi.softAPIP();
}

int WiFiManager::getAPClientCount() const
{
    return WiFi.softAPgetStationNum();
}

void WiFiManager::attemptReconnect()
{
    if (!apModeActive && WiFi.status() != WL_CONNECTED)
    {
        logTimestamp();
        Serial.println("WiFi: Attempting reconnection...");
        WiFi.reconnect();
    }
}

void WiFiManager::generateAPName()
{
    // Create unique AP name using last 4 chars of MAC
    uint8_t mac[6];
    WiFi.macAddress(mac);
    snprintf(apSSID, sizeof(apSSID), "%s%02X%02X", AP_SSID_PREFIX, mac[4], mac[5]);
}

void WiFiManager::generateRandomPassword()
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
