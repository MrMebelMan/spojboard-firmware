#ifndef CONFIGWEBSERVER_H
#define CONFIGWEBSERVER_H

#include <WebServer.h>
#include "../config/AppConfig.h"
#include "../api/DepartureData.h"
#include "OTAUpdateManager.h"
#include "GitHubOTA.h"

// Forward declaration
class DisplayManager;

// ============================================================================
// Configuration Web Server
// ============================================================================

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

    ConfigWebServer();
    ~ConfigWebServer();

    /**
     * Initialize and start web server
     * @return true if server started successfully
     */
    bool begin();

    /**
     * Stop web server
     */
    void stop();

    /**
     * Handle client requests (call from main loop)
     */
    void handleClient();

    /**
     * Set callback functions for configuration events
     */
    void setCallbacks(ConfigSaveCallback onSave, RefreshCallback onRefresh, RebootCallback onReboot);

    /**
     * Set display manager for OTA progress updates
     * @param displayMgr Pointer to DisplayManager instance
     */
    void setDisplayManager(DisplayManager* displayMgr);

    /**
     * Update current state for status display
     * @param config Current configuration
     * @param wifiConnected WiFi connection status
     * @param apModeActive AP mode status
     * @param apSSID AP network name (if in AP mode)
     * @param apPassword AP password (if in AP mode)
     * @param apClientCount Number of connected AP clients
     * @param apiError API error status
     * @param apiErrorMsg API error message
     * @param departureCount Number of departures
     * @param stopName Current stop name
     */
    void updateState(const Config* config,
                    bool wifiConnected, bool apModeActive,
                    const char* apSSID, const char* apPassword, int apClientCount,
                    bool apiError, const char* apiErrorMsg,
                    int departureCount, const char* stopName);

    /**
     * Get web server instance for direct access
     */
    WebServer* getServer() { return server; }

private:
    WebServer* server;
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

    // HTTP handlers
    void handleRoot();
    void handleSave();
    void handleRefresh();
    void handleReboot();
    void handleClearConfig();   // POST: clear all settings (factory reset)
    void handleUpdate();        // GET: show OTA upload form
    void handleUpdateUpload();  // POST: handle firmware upload
    void handleCheckUpdate();   // GET: check GitHub for updates (AJAX)
    void handleDownloadUpdate(); // POST: download and install from GitHub (AJAX)
    void handleNotFound();

    // OTA progress callbacks (static for use as function pointers)
    static void otaProgressCallback(size_t progress, size_t total);
    static void githubOtaProgressCallback(size_t progress, size_t total);
    static ConfigWebServer* instanceForCallback;  // Static instance pointer for callbacks

    // HTML templates
    static const char* HTML_HEADER;
    static const char* HTML_FOOTER;
};

#endif // CONFIGWEBSERVER_H
