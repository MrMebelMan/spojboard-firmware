#include "ConfigWebServer.h"
#include "../utils/Logger.h"
#include "../display/DisplayManager.h"
#include <WiFi.h>
#include <Update.h>
#include <string.h>

// HTML Templates
// Static instance pointer for OTA callback
ConfigWebServer* ConfigWebServer::instanceForCallback = nullptr;

const char* ConfigWebServer::HTML_HEADER = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>SpojBoard Configuration</title>
    <style>
        body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
               max-width: 600px; margin: 0 auto; padding: 20px; background: #1a1a2e; color: #eee; }
        h1 { color: #00d4ff; }
        h2 { color: #ff6b6b; margin-top: 30px; }
        .card { background: #16213e; border-radius: 10px; padding: 20px; margin: 15px 0; }
        label { display: block; margin: 10px 0 5px; color: #aaa; font-size: 0.9em; }
        input, select { width: 100%; padding: 12px; border: 1px solid #333; border-radius: 5px;
                        background: #0f0f23; color: #fff; box-sizing: border-box; font-size: 16px; }
        input:focus { border-color: #00d4ff; outline: none; }
        button { background: #00d4ff; color: #000; padding: 15px 30px; border: none;
                 border-radius: 5px; cursor: pointer; font-size: 16px; margin-top: 20px; width: 100%; }
        button:hover { background: #00a8cc; }
        button.danger { background: #ff4757; color: #fff; }
        button.danger:hover { background: #ff6b81; }
        .status { padding: 15px; border-radius: 5px; margin: 10px 0; }
        .status.ok { background: #2ed573; color: #000; }
        .status.error { background: #ff4757; }
        .status.warn { background: #ffa502; color: #000; }
        .info { color: #888; font-size: 0.85em; margin-top: 5px; }
        a { color: #00d4ff; }
        .grid { display: grid; grid-template-columns: 1fr 1fr; gap: 15px; }
        @media (max-width: 500px) { .grid { grid-template-columns: 1fr; } }
    </style>
</head>
<body>
)rawliteral";

const char* ConfigWebServer::HTML_FOOTER = "</body></html>";

ConfigWebServer::ConfigWebServer()
    : server(nullptr), otaManager(nullptr), githubOTA(nullptr), displayManager(nullptr),
      currentConfig(nullptr),
      wifiConnected(false), apModeActive(false),
      apSSID(""), apPassword(""), apClientCount(0),
      apiError(false), apiErrorMsg(""), departureCount(0), stopName(""),
      onSaveCallback(nullptr), onRefreshCallback(nullptr), onRebootCallback(nullptr)
{
    otaManager = new OTAUpdateManager();
    githubOTA = new GitHubOTA();
    instanceForCallback = this;  // Set static instance for OTA callback
}

ConfigWebServer::~ConfigWebServer()
{
    stop();

    if (otaManager != nullptr)
    {
        delete otaManager;
        otaManager = nullptr;
    }

    if (githubOTA != nullptr)
    {
        delete githubOTA;
        githubOTA = nullptr;
    }
}

bool ConfigWebServer::begin()
{
    if (server != nullptr)
    {
        return true; // Already started
    }

    server = new WebServer(80);

    // Register handlers with lambda wrappers to access 'this'
    server->on("/", HTTP_GET, [this]() { handleRoot(); });
    server->on("/save", HTTP_POST, [this]() { handleSave(); });
    server->on("/refresh", HTTP_POST, [this]() { handleRefresh(); });
    server->on("/reboot", HTTP_POST, [this]() { handleReboot(); });
    server->on("/clear-config", HTTP_POST, [this]() { handleClearConfig(); });
    server->on("/update", HTTP_GET, [this]() { handleUpdate(); });
    server->on("/update", HTTP_POST,
        [this]() { handleUpdateUpload(); },  // Upload handler
        [this]() { handleUpdateUpload(); }   // Same function handles chunks
    );
    server->on("/check-update", HTTP_GET, [this]() { handleCheckUpdate(); });
    server->on("/download-update", HTTP_POST, [this]() { handleDownloadUpdate(); });
    server->onNotFound([this]() { handleNotFound(); });

    server->begin();

    // Initialize OTA manager
    if (otaManager != nullptr)
    {
        otaManager->begin();
    }

    logTimestamp();
    Serial.println("Web server started on port 80");

    return true;
}

void ConfigWebServer::stop()
{
    if (server != nullptr)
    {
        server->stop();
        delete server;
        server = nullptr;

        logTimestamp();
        Serial.println("Web server stopped");
    }
}

void ConfigWebServer::handleClient()
{
    if (server != nullptr)
    {
        server->handleClient();
    }
}

void ConfigWebServer::setCallbacks(ConfigSaveCallback onSave, RefreshCallback onRefresh, RebootCallback onReboot)
{
    onSaveCallback = onSave;
    onRefreshCallback = onRefresh;
    onRebootCallback = onReboot;
}

void ConfigWebServer::setDisplayManager(DisplayManager* displayMgr)
{
    displayManager = displayMgr;
}

void ConfigWebServer::updateState(const Config* config,
                                  bool connected, bool apMode,
                                  const char* ssid, const char* password, int clientCount,
                                  bool error, const char* errorMsg,
                                  int depCount, const char* stop)
{
    currentConfig = config;
    wifiConnected = connected;
    apModeActive = apMode;
    apSSID = ssid;
    apPassword = password;
    apClientCount = clientCount;
    apiError = error;
    apiErrorMsg = errorMsg;
    departureCount = depCount;
    stopName = stop;
}

void ConfigWebServer::handleRoot()
{
    if (currentConfig == nullptr)
    {
        server->send(500, "text/plain", "Server not initialized");
        return;
    }

    String html = HTML_HEADER;
    html += "<h1>🚌 SpojBoard</h1>";
    html += "<p style='text-align:center; color:#888; margin-top:-10px; margin-bottom:20px;'>Smart Panel for Onward Journeys</p>";

    // AP Mode banner
    if (apModeActive)
    {
        html += "<div class='card' style='background: #ff6b6b; color: #fff;'>";
        html += "<h2 style='color: #fff; margin-top: 0;'>⚠️ Setup Mode</h2>";
        html += "<p>Device is in Access Point mode. Configure WiFi credentials below to connect to your network.</p>";
        html += "<p><strong>AP Name:</strong> " + String(apSSID) + "</p>";
        html += "</div>";
    }

    // Status card
    html += "<div class='card'>";
    html += "<h2>Status</h2>";

    if (apModeActive)
    {
        html += "<div class='status warn'>AP Mode Active - Not connected to WiFi</div>";
        html += "<p><strong>Connected clients:</strong> " + String(apClientCount) + "</p>";
    }
    else if (wifiConnected)
    {
        html += "<div class='status ok'>WiFi Connected: " + WiFi.localIP().toString() + "</div>";
    }
    else
    {
        html += "<div class='status error'>WiFi Disconnected</div>";
    }

    if (!apModeActive)
    {
        if (strlen(currentConfig->apiKey) > 0)
        {
            if (apiError)
            {
                html += "<div class='status error'>API Error: " + String(apiErrorMsg) + "</div>";
            }
            else
            {
                html += "<div class='status ok'>API OK - " + String(departureCount) + " departures</div>";
            }
            html += "<p><strong>API Key:</strong> Configured (hidden)</p>";
        }
        else
        {
            html += "<div class='status warn'>API Key not configured</div>";
        }

        if (stopName[0])
        {
            html += "<p><strong>Stop:</strong> " + String(stopName) + "</p>";
        }
    }

    html += "<p><strong>Free Memory:</strong> " + String(ESP.getFreeHeap()) + " bytes</p>";

    // Format firmware version with build ID (8 hex chars)
    char buildIdStr[10];
    snprintf(buildIdStr, sizeof(buildIdStr), "%08x", BUILD_ID);
    html += "<p><strong>Firmware:</strong> Release " + String(FIRMWARE_RELEASE) + " (" + String(buildIdStr) + ")</p>";
    html += "</div>";

    // Configuration form
    html += "<div class='card'>";
    html += "<h2>Configuration</h2>";
    html += "<form method='POST' action='/save'>";

    html += "<label>WiFi SSID</label>";
    html += "<input type='text' name='ssid' value='" + String(currentConfig->wifiSsid) + "' required placeholder='Your WiFi network name'>";

    html += "<label>WiFi Password</label>";
    html += "<input type='password' name='password' placeholder='Enter WiFi password'>";
    if (!apModeActive)
    {
        html += "<p class='info'>Leave empty to keep current password</p>";
    }
    else
    {
        html += "<p class='info'>Enter your WiFi password</p>";
    }

    html += "<label>Golemio API Key</label>";
    // API key is required if: in AP mode OR current API key is empty
    bool apiKeyRequired = apModeActive || strlen(currentConfig->apiKey) == 0;
    if (apiKeyRequired)
    {
        html += "<input type='password' name='apikey' placeholder='Enter Golemio API key' required>";
        html += "<p class='info'>Required: Get your API key at <a href='https://api.golemio.cz/api-keys/' target='_blank'>api.golemio.cz</a></p>";
    }
    else
    {
        html += "<input type='password' name='apikey' placeholder='Enter Golemio API key'>";
        html += "<p class='info'>Leave empty to keep current API key. Get a new key at <a href='https://api.golemio.cz/api-keys/' target='_blank'>api.golemio.cz</a></p>";
    }

    html += "<label>Stop ID(s)</label>";
    html += "<input type='text' name='stops' value='" + String(currentConfig->stopIds) + "' required placeholder='e.g., U693Z2P'>";
    html += "<p class='info'>Comma-separated PID stop IDs. Find IDs at <a href='http://data.pid.cz/stops/xml/StopsByName.xml' target='_blank'>PID data</a></p>";

    html += "<div class='grid'>";
    html += "<div><label>Refresh Interval (sec)</label>";
    html += "<input type='number' name='refresh' value='" + String(currentConfig->refreshInterval) + "' min='10' max='300'></div>";

    html += "<div><label>Number of Departures</label>";
    html += "<input type='number' name='numdeps' value='" + String(currentConfig->numDepartures) + "' min='1' max='6'></div>";

    html += "<div><label>Min Departure Time (min)</label>";
    html += "<input type='number' name='mindeptime' value='" + String(currentConfig->minDepartureTime) + "' min='0' max='30'></div>";

    html += "<div><label>Display Brightness (0-255)</label>";
    html += "<input type='number' name='brightness' value='" + String(currentConfig->brightness) + "' min='0' max='255'></div>";
    html += "</div>";

    if (apModeActive)
    {
        html += "<button type='submit'>Save & Connect to WiFi</button>";
    }
    else
    {
        html += "<button type='submit'>Save Configuration</button>";
    }
    html += "</form>";
    html += "</div>";

    // Actions
    if (!apModeActive)
    {
        html += "<div class='card'>";
        html += "<h2>Actions</h2>";
        html += "<form method='POST' action='/refresh' style='display:inline'>";
        html += "<button type='submit'>Refresh Now</button>";
        html += "</form>";
        html += "<form method='GET' action='/update' style='display:inline; margin-top:10px'>";
        html += "<button type='submit'>Update Firmware</button>";
        html += "</form>";
        html += "<form id='checkUpdateForm' onsubmit='checkForUpdate(event); return false;' style='display:inline; margin-top:10px'>";
        html += "<button type='submit' id='checkUpdateBtn'>Check for Updates</button>";
        html += "</form>";
        html += "<form method='POST' action='/reboot' style='display:inline; margin-top:10px'>";
        html += "<button type='submit' class='danger'>Reboot Device</button>";
        html += "</form>";
        html += "<form method='POST' action='/clear-config' onsubmit='return confirm(\"⚠️ WARNING: This will erase ALL settings and reboot into setup mode. Continue?\");' style='display:inline; margin-top:10px'>";
        html += "<button type='submit' class='danger'>Clear All Settings</button>";
        html += "</form>";
        html += "<div id='updateStatus' style='display:none; margin-top:15px;'></div>";
        html += "</div>";
    }

    // JavaScript for GitHub updates
    if (!apModeActive)
    {
        html += R"rawliteral(
<script>
async function checkForUpdate(event) {
    event.preventDefault();
    const btn = document.getElementById('checkUpdateBtn');
    const status = document.getElementById('updateStatus');

    btn.disabled = true;
    btn.innerText = 'Checking...';
    status.style.display = 'none';

    try {
        const response = await fetch('/check-update');
        const data = await response.json();

        if (data.error) {
            throw new Error(data.error);
        }

        if (data.available) {
            status.innerHTML = `
                <div class='card' style='background: #2ed573; color: #000;'>
                    <h3 style='margin-top:0;'>✨ Update Available!</h3>
                    <p><strong>Version:</strong> ${data.releaseName}</p>
                    <p><strong>File:</strong> ${data.fileName} (${formatBytes(data.fileSize)})</p>
                    <details style='margin: 10px 0;'>
                        <summary style='cursor:pointer; font-weight:bold;'>Release Notes</summary>
                        <div style='margin-top:10px; white-space:pre-wrap; font-size:0.9em;'>${escapeHtml(data.releaseNotes)}</div>
                    </details>
                    <button onclick="downloadUpdate('${data.assetUrl}', ${data.fileSize})"
                            style='background:#ff6b6b; color:#fff;'>
                        Download & Install
                    </button>
                </div>
            `;
        } else {
            status.innerHTML = `
                <div class='status ok'>✓ You're up to date!</div>
            `;
        }
        status.style.display = 'block';
    } catch (error) {
        status.innerHTML = `
            <div class='status error'>Error: ${error.message}</div>
        `;
        status.style.display = 'block';
    } finally {
        btn.disabled = false;
        btn.innerText = 'Check for Updates';
    }
}

async function downloadUpdate(url, size) {
    if (!confirm('Download and install firmware? Device will reboot after installation.')) {
        return;
    }

    const status = document.getElementById('updateStatus');
    status.innerHTML = `
        <div class='card'>
            <h3>⬇️ Downloading Firmware...</h3>
            <p style='color:#888; font-size:0.9em;'>Do not power off or disconnect!</p>
            <div style='background:#333; border-radius:5px; overflow:hidden; height:30px; margin:15px 0;'>
                <div id='downloadProgress' style='background:#00d4ff; height:100%; width:0%; transition:width 0.3s;'></div>
            </div>
            <p id='downloadText' style='text-align:center;'>Starting download...</p>
        </div>
    `;

    try {
        const response = await fetch('/download-update', {
            method: 'POST',
            headers: {'Content-Type': 'application/json'},
            body: JSON.stringify({ assetUrl: url, expectedSize: size })
        });

        const data = await response.json();

        if (data.success) {
            status.innerHTML = `
                <div class='status ok'>
                    ✅ Update installed successfully! Device rebooting...
                </div>
            `;
            setTimeout(() => window.location.reload(), 8000);
        } else {
            status.innerHTML = `
                <div class='status error'>
                    ❌ Installation failed: ${data.error}
                </div>
            `;
        }
    } catch (error) {
        status.innerHTML = `
            <div class='status error'>
                ❌ Download failed: ${error.message}
            </div>
        `;
    }
}

function formatBytes(bytes) {
    if (bytes < 1024) return bytes + ' B';
    if (bytes < 1048576) return (bytes / 1024).toFixed(1) + ' KB';
    return (bytes / 1048576).toFixed(1) + ' MB';
}

function escapeHtml(text) {
    const div = document.createElement('div');
    div.textContent = text;
    return div.innerHTML;
}
</script>
)rawliteral";
    }

    html += HTML_FOOTER;
    server->send(200, "text/html", html);
}

void ConfigWebServer::handleSave()
{
    if (currentConfig == nullptr || onSaveCallback == nullptr)
    {
        server->send(500, "text/plain", "Server not initialized");
        return;
    }

    // Create a copy of config to modify
    Config newConfig = *currentConfig;
    bool wifiChanged = false;

    if (server->hasArg("ssid"))
    {
        String newSsid = server->arg("ssid");
        if (newSsid != newConfig.wifiSsid)
        {
            wifiChanged = true;
        }
        strlcpy(newConfig.wifiSsid, newSsid.c_str(), sizeof(newConfig.wifiSsid));
    }
    if (server->hasArg("password") && server->arg("password").length() > 0)
    {
        strlcpy(newConfig.wifiPassword, server->arg("password").c_str(), sizeof(newConfig.wifiPassword));
        wifiChanged = true;
    }
    if (server->hasArg("apikey") && server->arg("apikey").length() > 0)
    {
        strlcpy(newConfig.apiKey, server->arg("apikey").c_str(), sizeof(newConfig.apiKey));
    }
    if (server->hasArg("stops"))
    {
        strlcpy(newConfig.stopIds, server->arg("stops").c_str(), sizeof(newConfig.stopIds));
    }
    if (server->hasArg("refresh"))
    {
        newConfig.refreshInterval = server->arg("refresh").toInt();
        if (newConfig.refreshInterval < 10)
            newConfig.refreshInterval = 10;
        if (newConfig.refreshInterval > 300)
            newConfig.refreshInterval = 300;
    }
    if (server->hasArg("numdeps"))
    {
        newConfig.numDepartures = server->arg("numdeps").toInt();
        if (newConfig.numDepartures < 1)
            newConfig.numDepartures = 1;
        if (newConfig.numDepartures > MAX_DEPARTURES)
            newConfig.numDepartures = MAX_DEPARTURES;
    }
    if (server->hasArg("mindeptime"))
    {
        newConfig.minDepartureTime = server->arg("mindeptime").toInt();
        if (newConfig.minDepartureTime < 0)
            newConfig.minDepartureTime = 0;
        if (newConfig.minDepartureTime > 30)
            newConfig.minDepartureTime = 30;
    }
    if (server->hasArg("brightness"))
    {
        newConfig.brightness = server->arg("brightness").toInt();
        if (newConfig.brightness < 0)
            newConfig.brightness = 0;
        if (newConfig.brightness > 255)
            newConfig.brightness = 255;
    }

    newConfig.configured = true;

    // If in AP mode or WiFi changed, show restart message
    if (apModeActive || wifiChanged)
    {
        String html = HTML_HEADER;
        html += "<h1>⏳ Connecting...</h1>";
        html += "<p>Attempting to connect to WiFi network: <strong>" + String(newConfig.wifiSsid) + "</strong></p>";
        html += "<p>Please wait... The device will restart and connect to the new network.</p>";
        html += "<p>If connection fails, the device will return to AP mode.</p>";
        html += "<div class='card'>";
        html += "<p>After successful connection, access the device at its new IP address on your network.</p>";
        html += "</div>";
        html += HTML_FOOTER;
        server->send(200, "text/html", html);
    }
    else
    {
        // Normal save without WiFi change
        String html = HTML_HEADER;
        html += "<h1>✅ Configuration Saved</h1>";
        html += "<p>Settings have been saved. The device will apply them immediately.</p>";
        html += "<p><a href='/'>← Back to Dashboard</a></p>";
        html += HTML_FOOTER;
        server->send(200, "text/html", html);
    }

    // Call the callback to notify main.cpp
    onSaveCallback(newConfig, wifiChanged);
}

void ConfigWebServer::handleRefresh()
{
    if (onRefreshCallback != nullptr)
    {
        onRefreshCallback();
    }

    server->sendHeader("Location", "/");
    server->send(302, "text/plain", "");
}

void ConfigWebServer::handleReboot()
{
    String html = HTML_HEADER;
    html += "<h1>🔄 Rebooting...</h1>";
    html += "<p>The device is rebooting. Please wait a few seconds.</p>";
    html += "<script>setTimeout(function(){ window.location='/'; }, 5000);</script>";
    html += HTML_FOOTER;
    server->send(200, "text/html", html);

    if (onRebootCallback != nullptr)
    {
        onRebootCallback();
    }
}

void ConfigWebServer::handleClearConfig()
{
    String html = HTML_HEADER;
    html += "<h1>🗑️ Clearing All Settings...</h1>";
    html += "<div class='card' style='background: #ff6b6b; color: #fff;'>";
    html += "<p>All configuration has been erased from flash memory.</p>";
    html += "<p>The device will reboot into AP (setup) mode in 3 seconds.</p>";
    html += "<p>You will need to reconfigure WiFi and API settings.</p>";
    html += "</div>";
    html += "<script>setTimeout(function(){ window.location='/'; }, 8000);</script>";
    html += HTML_FOOTER;
    server->send(200, "text/html", html);

    // Clear all config from NVS
    clearConfig();

    // Reboot after a short delay
    delay(3000);
    ESP.restart();
}

void ConfigWebServer::handleUpdate()
{
    // Block OTA upload in AP mode (security measure)
    if (apModeActive)
    {
        String html = HTML_HEADER;
        html += "<h1>⚠️ OTA Update Unavailable</h1>";
        html += "<div class='card' style='background: #ff6b6b; color: #fff;'>";
        html += "<p>Firmware updates are disabled in AP (setup) mode for security reasons.</p>";
        html += "<p>Please connect the device to your WiFi network first.</p>";
        html += "</div>";
        html += "<p><a href='/'>← Back to Dashboard</a></p>";
        html += HTML_FOOTER;
        server->send(403, "text/html", html);
        return;
    }

    // Show OTA upload form
    String html = HTML_HEADER;
    html += "<h1>🔧 Firmware Update</h1>";

    // Warning card
    html += "<div class='card' style='background: #ff6b6b; color: #fff;'>";
    html += "<h3 style='color: #fff; margin-top: 0;'>⚠️ Important</h3>";
    html += "<ul style='margin: 10px 0; padding-left: 20px;'>";
    html += "<li>Do NOT power off or disconnect during update!</li>";
    html += "<li>Update takes 1-2 minutes to complete</li>";
    html += "<li>Device will reboot automatically after update</li>";
    html += "<li>Make sure you upload the correct .bin file for ESP32-S3</li>";
    html += "</ul>";
    html += "</div>";

    // Current firmware info
    html += "<div class='card'>";
    html += "<h2>Current Firmware</h2>";
    char currentBuildId[10];
    snprintf(currentBuildId, sizeof(currentBuildId), "%08x", BUILD_ID);
    html += "<p><strong>Release:</strong> " + String(FIRMWARE_RELEASE) + "</p>";
    html += "<p><strong>Build ID:</strong> " + String(currentBuildId) + "</p>";
    html += "</div>";

    // Upload form
    html += "<div class='card'>";
    html += "<h2>Upload New Firmware</h2>";
    html += "<form method='POST' action='/update' enctype='multipart/form-data' id='uploadForm'>";
    html += "<input type='file' name='firmware' accept='.bin' required style='margin-bottom: 15px;'>";
    html += "<button type='submit' id='uploadBtn'>Upload Firmware</button>";
    html += "</form>";
    html += "<div id='progress' style='display:none; margin-top:20px;'>";
    html += "<div style='background:#333; border-radius:5px; overflow:hidden; height:30px;'>";
    html += "<div id='progressBar' style='background:#00d4ff; height:100%; width:0%; transition:width 0.3s;'></div>";
    html += "</div>";
    html += "<p id='progressText' style='text-align:center; margin-top:10px;'>Uploading...</p>";
    html += "</div>";
    html += "</div>";

    // JavaScript for progress
    html += R"rawliteral(
<script>
document.getElementById('uploadForm').onsubmit = function() {
    document.getElementById('uploadBtn').disabled = true;
    document.getElementById('progress').style.display = 'block';
    document.getElementById('progressText').innerText = 'Uploading firmware...';
};
</script>
)rawliteral";

    html += "<p><a href='/'>← Back to Dashboard</a></p>";
    html += HTML_FOOTER;
    server->send(200, "text/html", html);
}

void ConfigWebServer::otaProgressCallback(size_t progress, size_t total)
{
    // Static callback that forwards to instance method
    if (instanceForCallback != nullptr && instanceForCallback->displayManager != nullptr)
    {
        instanceForCallback->displayManager->drawOTAProgress(progress, total);
    }
}

void ConfigWebServer::handleUpdateUpload()
{
    if (otaManager == nullptr)
    {
        server->send(500, "text/plain", "OTA manager not initialized");
        return;
    }

    // Block uploads in AP mode
    if (apModeActive)
    {
        server->send(403, "text/plain", "OTA updates disabled in AP mode");
        return;
    }

    // Let OTA manager handle the upload with progress callback
    otaManager->handleUpload(server, otaProgressCallback);

    // Check if upload finished
    HTTPUpload& upload = server->upload();
    if (upload.status == UPLOAD_FILE_END)
    {
        // Send response based on success/failure
        if (strlen(otaManager->getError()) > 0)
        {
            // Error occurred
            String html = HTML_HEADER;
            html += "<h1>❌ Update Failed</h1>";
            html += "<div class='card' style='background: #ff6b6b; color: #fff;'>";
            html += "<p><strong>Error:</strong> " + String(otaManager->getError()) + "</p>";
            html += "</div>";
            html += "<p><a href='/update'>← Try Again</a></p>";
            html += "<p><a href='/'>← Back to Dashboard</a></p>";
            html += HTML_FOOTER;
            server->send(500, "text/html", html);
        }
        else
        {
            // Success
            String html = HTML_HEADER;
            html += "<h1>✅ Update Successful!</h1>";
            html += "<div class='card' style='background: #2ed573; color: #000;'>";
            html += "<p>Firmware has been uploaded and validated successfully.</p>";
            html += "<p>The device will reboot in 3 seconds...</p>";
            html += "</div>";
            html += "<script>setTimeout(function(){ window.location='/'; }, 8000);</script>";
            html += HTML_FOOTER;
            server->send(200, "text/html", html);

            // Reboot after a short delay
            delay(3000);
            ESP.restart();
        }
    }
}

void ConfigWebServer::handleNotFound()
{
    // Captive portal redirect - redirect all unknown requests to root
    if (apModeActive)
    {
        server->sendHeader("Location", "http://192.168.4.1/");
        server->send(302, "text/plain", "");
    }
    else
    {
        server->sendHeader("Location", "/");
        server->send(302, "text/plain", "");
    }
}

void ConfigWebServer::githubOtaProgressCallback(size_t progress, size_t total)
{
    // Static callback that forwards to instance method
    if (instanceForCallback != nullptr &&
        instanceForCallback->displayManager != nullptr)
    {
        instanceForCallback->displayManager->drawOTAProgress(progress, total);
    }
}

// Helper function to escape JSON strings
String escapeJsonString(const char* str)
{
    String result = "";
    if (!str) return result;

    for (size_t i = 0; str[i] != '\0'; i++)
    {
        char c = str[i];
        switch (c)
        {
            case '"':  result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            case '\b': result += "\\b"; break;
            case '\f': result += "\\f"; break;
            default:
                // Skip other control characters
                if (c >= 0 && c < 32)
                {
                    // Skip control characters
                }
                else
                {
                    result += c;
                }
                break;
        }
    }
    return result;
}

void ConfigWebServer::handleCheckUpdate()
{
    // Block if in AP mode
    if (apModeActive)
    {
        server->send(403, "application/json", "{\"error\":\"Updates not available in AP mode\"}");
        return;
    }

    logTimestamp();
    Serial.println("Checking for GitHub updates...");

    // Check for updates
    GitHubOTA::ReleaseInfo info = githubOTA->checkForUpdate(FIRMWARE_RELEASE);

    // Build JSON response with properly escaped strings
    String json = "{";

    if (info.hasError)
    {
        json += "\"available\":false,";
        json += "\"error\":\"" + escapeJsonString(info.errorMsg) + "\"";
    }
    else if (info.available)
    {
        json += "\"available\":true,";
        json += "\"releaseNumber\":" + String(info.releaseNumber) + ",";
        json += "\"releaseName\":\"" + escapeJsonString(info.releaseName) + "\",";
        json += "\"releaseNotes\":\"" + escapeJsonString(info.releaseNotes) + "\",";
        json += "\"fileName\":\"" + escapeJsonString(info.assetName) + "\",";
        json += "\"fileSize\":" + String(info.assetSize) + ",";
        json += "\"assetUrl\":\"" + escapeJsonString(info.assetUrl) + "\"";
    }
    else
    {
        json += "\"available\":false";
    }

    json += "}";

    server->send(200, "application/json", json);
}

void ConfigWebServer::handleDownloadUpdate()
{
    // Block if in AP mode
    if (apModeActive)
    {
        server->send(403, "application/json", "{\"success\":false,\"error\":\"Updates not available in AP mode\"}");
        return;
    }

    // Parse JSON request body
    String body = server->arg("plain");

    // Simple JSON parsing (extract assetUrl and expectedSize)
    int urlStart = body.indexOf("\"assetUrl\":\"") + 12;
    int urlEnd = body.indexOf("\"", urlStart);
    String assetUrl = body.substring(urlStart, urlEnd);

    int sizeStart = body.indexOf("\"expectedSize\":") + 15;
    int sizeEnd = body.indexOf(",", sizeStart);
    if (sizeEnd < 0) sizeEnd = body.indexOf("}", sizeStart);
    String sizeStr = body.substring(sizeStart, sizeEnd);
    size_t expectedSize = sizeStr.toInt();

    if (assetUrl.length() == 0 || expectedSize == 0)
    {
        server->send(400, "application/json", "{\"success\":false,\"error\":\"Invalid request parameters\"}");
        return;
    }

    logTimestamp();
    Serial.print("Downloading update from: ");
    Serial.println(assetUrl);

    // Download and install
    bool success = githubOTA->downloadAndInstall(assetUrl.c_str(), expectedSize, githubOtaProgressCallback);

    if (success)
    {
        server->send(200, "application/json", "{\"success\":true,\"message\":\"Rebooting...\"}");

        logTimestamp();
        Serial.println("Update successful, rebooting in 3 seconds...");

        delay(3000);
        ESP.restart();
    }
    else
    {
        server->send(500, "application/json", "{\"success\":false,\"error\":\"Download or installation failed\"}");
    }
}
