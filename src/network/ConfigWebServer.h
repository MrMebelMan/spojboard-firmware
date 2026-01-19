#ifndef CONFIGWEBSERVER_H
#define CONFIGWEBSERVER_H

#include <Arduino.h>
#include "../config/AppConfig.h"
#include "../api/DepartureData.h"
#include "../display/DisplayManager.h"

// ============================================================================
// Configuration Web Server
// ============================================================================

#if defined(MATRIX_PORTAL_M4)
// ==========================================================================
// M4 Simple Implementation - Basic web server for /on and /off only
// ==========================================================================

#include <WiFiNINA.h>

class ConfigWebServer
{
public:
    typedef void (*ConfigSaveCallback)(const Config& newConfig, bool wifiChanged);
    typedef void (*RefreshCallback)();
    typedef void (*RebootCallback)();
    typedef void (*DemoStartCallback)(const Departure* demoDepartures, int demoCount);
    typedef void (*DemoStopCallback)();

    ConfigWebServer() : server(80), displayManager(nullptr), currentConfig(nullptr) {}
    ~ConfigWebServer() {}

    bool begin() {
        server.begin();
        Serial.println("Web server started on port 80 (M4 minimal)");
        return true;
    }
    void stop() {}

    void handleClient() {
        WiFiClient client = server.available();
        if (client) {
            String request = "";
            while (client.connected()) {
                if (client.available()) {
                    char c = client.read();
                    request += c;
                    if (c == '\n' && request.endsWith("\r\n\r\n")) {
                        break;
                    }
                    if (request.length() > 200) break; // Limit request size
                }
            }

            if (request.indexOf("GET /off") >= 0) {
                if (displayManager) displayManager->turnOff();
                client.println("HTTP/1.1 200 OK");
                client.println("Content-Type: text/plain");
                client.println("Connection: close");
                client.println();
                client.println("OK");
            }
            else if (request.indexOf("GET /on") >= 0) {
                if (displayManager) displayManager->turnOn();
                client.println("HTTP/1.1 200 OK");
                client.println("Content-Type: text/plain");
                client.println("Connection: close");
                client.println();
                client.println("OK");
            }
            else {
                client.println("HTTP/1.1 404 Not Found");
                client.println("Connection: close");
                client.println();
            }
            delay(1);
            client.stop();
        }
    }

    void setCallbacks(ConfigSaveCallback, RefreshCallback, RebootCallback,
                     DemoStartCallback = nullptr, DemoStopCallback = nullptr) {}

    void setDisplayManager(DisplayManager* dm) { displayManager = dm; }

    void updateState(const Config* cfg, bool, bool, const char*, const char*, int,
                    bool, const char*, int, const char*) { currentConfig = cfg; }

    void* getServer() { return nullptr; }

private:
    WiFiServer server;
    DisplayManager* displayManager;
    const Config* currentConfig;
};

#else
// ==========================================================================
// ESP32 Full Implementation
// ==========================================================================

#include <WebServer.h>
#include "OTAUpdateManager.h"
#include "GitHubOTA.h"

typedef WebServer WebServerType;

/**
 * Web server for device configuration and status display.
 * Provides HTML interface for WiFi, API, and display settings.
 */
class ConfigWebServer
{
public:
    // Callback types for configuration events
    typedef void (*ConfigSaveCallback)(const Config& newConfig, bool wifiChanged);
    typedef void (*RefreshCallback)();
    typedef void (*RebootCallback)();
    typedef void (*DemoStartCallback)(const Departure* demoDepartures, int demoCount);
    typedef void (*DemoStopCallback)();

    ConfigWebServer();
    ~ConfigWebServer();

    bool begin();
    void stop();
    void handleClient();

    void setCallbacks(ConfigSaveCallback onSave, RefreshCallback onRefresh, RebootCallback onReboot,
                     DemoStartCallback onDemoStart = nullptr, DemoStopCallback onDemoStop = nullptr);

    void setDisplayManager(DisplayManager* displayMgr);

    void updateState(const Config* config,
                    bool wifiConnected, bool apModeActive,
                    const char* apSSID, const char* apPassword, int apClientCount,
                    bool apiError, const char* apiErrorMsg,
                    int departureCount, const char* stopName);

    WebServerType* getServer() { return server; }

private:
    WebServerType* server;
    OTAUpdateManager* otaManager;
    GitHubOTA* githubOTA;
    DisplayManager* displayManager;

    // Current state (for status display)
    const Config* currentConfig;
    bool wifiConnected;
    bool apModeActive;
    const char* apSSID;
    const char* apPassword;
    int apClientCount;
    bool apiError;
    const char* apiErrorMsg;
    int departureCount;
    const char* stopName;

    // Callbacks
    ConfigSaveCallback onSaveCallback;
    RefreshCallback onRefreshCallback;
    RebootCallback onRebootCallback;
    DemoStartCallback onDemoStartCallback;
    DemoStopCallback onDemoStopCallback;

    // HTTP handlers
    void handleRoot();
    void handleSave();
    void handleRefresh();
    void handleReboot();
    void handleClearConfig();
    void handleUpdate();
    void handleUpdateProgress();
    void handleUpdateComplete();
    void handleCheckUpdate();
    void handleDownloadUpdate();
    void handleDemo();
    void handleStartDemo();
    void handleStopDemo();
    void handleScreenOn();
    void handleScreenOff();
    void handleNotFound();

    // OTA progress callbacks
    static void otaProgressCallback(size_t progress, size_t total);
    static void githubOtaProgressCallback(size_t progress, size_t total);
    static ConfigWebServer* instanceForCallback;

    // HTML templates
    static const char* HTML_HEADER;
    static const char* HTML_FOOTER;
};

#endif // MATRIX_PORTAL_M4

#endif // CONFIGWEBSERVER_H
