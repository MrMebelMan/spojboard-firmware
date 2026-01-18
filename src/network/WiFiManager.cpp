#include "WiFiManager.h"
#include "../utils/Logger.h"
#include "../display/DisplayColors.h"

#if !defined(MATRIX_PORTAL_M4)
    #include <esp_random.h>
#endif

WiFiManager::WiFiManager()
    : apModeActive(false)
{
    apSSID[0] = '\0';
    apPassword[0] = '\0';
}

bool WiFiManager::connectSTA(const Config& config, int maxAttempts, int delayMs)
{
    char msg[96];
    snprintf(msg, sizeof(msg), "WiFi: Connecting to %s", config.wifiSsid);
    logTimestamp();
    debugPrintln(msg);

#if defined(MATRIX_PORTAL_M4)
    // WiFiNINA - check if WiFi module is present
    if (WiFi.status() == WL_NO_MODULE)
    {
        logTimestamp();
        debugPrintln("WiFi: Module not found!");
        return false;
    }

    // Start connection
    WiFi.begin(config.wifiSsid, config.wifiPassword);
#else
    // ESP32 - disconnect and reset WiFi first
    WiFi.disconnect(true);
    delay(500);
    WiFi.mode(WIFI_STA);
    delay(2000);
    WiFi.begin(config.wifiSsid, config.wifiPassword);
#endif

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < maxAttempts)
    {
        delay(delayMs);
        debugPrint(".");
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED)
    {
        IPAddress ip = WiFi.localIP();
        snprintf(msg, sizeof(msg), "\nWiFi: Connected! IP: %d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
        logTimestamp();
        debugPrintln(msg);
        return true;
    }
    else
    {
        logTimestamp();
        debugPrintln("\nWiFi: Connection failed!");
        return false;
    }
}

bool WiFiManager::startAP()
{
    logTimestamp();
    debugPrintln("Starting AP Mode...");

    // Generate credentials
    generateAPName();
    generateRandomPassword();

#if defined(MATRIX_PORTAL_M4)
    // WiFiNINA AP mode
    // Disconnect from any existing connection first
    WiFi.end();
    delay(100);

    // Start AP with password
    int status = WiFi.beginAP(apSSID, apPassword);
    if (status != WL_AP_LISTENING)
    {
        logTimestamp();
        debugPrintln("AP Mode failed to start!");
        return false;
    }
#else
    // ESP32 AP mode
    // Stop any existing WiFi connection
    WiFi.disconnect(true);
    delay(100);

    // Configure AP
    WiFi.mode(WIFI_AP);
    if (!WiFi.softAP(apSSID, apPassword))
    {
        logTimestamp();
        debugPrintln("AP Mode failed to start!");
        return false;
    }

    // Configure AP IP (default is 192.168.4.1)
    IPAddress apIP(192, 168, 4, 1);
    IPAddress gateway(192, 168, 4, 1);
    IPAddress subnet(255, 255, 255, 0);
    WiFi.softAPConfig(apIP, gateway, subnet);
#endif

    apModeActive = true;

    char msg[128];
    logTimestamp();
    debugPrintln("AP Mode Active!");
    snprintf(msg, sizeof(msg), "  SSID: %s", apSSID);
    debugPrintln(msg);
    snprintf(msg, sizeof(msg), "  Password: %s", apPassword);
    debugPrintln(msg);

    IPAddress ip = getAPIP();
    snprintf(msg, sizeof(msg), "  IP: %d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
    debugPrintln(msg);

    return true;
}

void WiFiManager::stopAP()
{
    if (apModeActive)
    {
        logTimestamp();
        debugPrintln("Stopping AP Mode...");

#if defined(MATRIX_PORTAL_M4)
        WiFi.end();
#else
        WiFi.softAPdisconnect(true);
#endif

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
#if defined(MATRIX_PORTAL_M4)
    // WiFiNINA uses localIP() for AP mode too
    return WiFi.localIP();
#else
    return WiFi.softAPIP();
#endif
}

int WiFiManager::getAPClientCount() const
{
#if defined(MATRIX_PORTAL_M4)
    // WiFiNINA doesn't provide client count
    return 0;
#else
    return WiFi.softAPgetStationNum();
#endif
}

void WiFiManager::attemptReconnect()
{
    if (!apModeActive && WiFi.status() != WL_CONNECTED)
    {
        logTimestamp();
        debugPrintln("WiFi: Attempting reconnection...");
#if defined(MATRIX_PORTAL_M4)
        // WiFiNINA - need to disconnect and reconnect
        // Note: This requires stored credentials
        WiFi.disconnect();
        delay(100);
#else
        WiFi.reconnect();
#endif
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

#if defined(MATRIX_PORTAL_M4)
    // Use analog noise for seed on M4
    randomSeed(analogRead(A0) ^ millis());
#else
    // Use hardware RNG on ESP32
    randomSeed(esp_random());
#endif

    for (int i = 0; i < 8; i++)
    {
        apPassword[i] = charset[random(0, strlen(charset))];
    }
    apPassword[8] = '\0';
}
