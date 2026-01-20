#include "ConfigWebServer.h"

// ConfigWebServer is stubbed in header for M4
#if !defined(MATRIX_PORTAL_M4)

#include "../utils/Logger.h"
#include "../display/DisplayManager.h"
#include <string.h>
#include <WiFi.h>
#include <Update.h>

static inline void systemRestart() { ESP.restart(); }

// Helper function to count stops in comma-separated list
static int countStops(const char* stopIds) {
    if (!stopIds || stopIds[0] == '\0') {
        return 0;
    }

    int count = 1;  // At least one stop if string is not empty
    for (const char* p = stopIds; *p; p++) {
        if (*p == ',') {
            count++;
        }
    }
    return count;
}

// HTML Templates
// Static instance pointer for OTA callback
ConfigWebServer *ConfigWebServer::instanceForCallback = nullptr;

const char *ConfigWebServer::HTML_HEADER = R"rawliteral(
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

const char *ConfigWebServer::HTML_FOOTER = "</body></html>";

ConfigWebServer::ConfigWebServer()
    : server(nullptr), otaManager(nullptr),
#if !defined(MATRIX_PORTAL_M4)
      githubOTA(nullptr),
#endif
      displayManager(nullptr),
      currentConfig(nullptr),
      wifiConnected(false), apModeActive(false),
      apSSID(""), apPassword(""), apClientCount(0),
      apiError(false), apiErrorMsg(""), departureCount(0), stopName(""),
      onSaveCallback(nullptr), onRefreshCallback(nullptr), onRebootCallback(nullptr),
      onDemoStartCallback(nullptr), onDemoStopCallback(nullptr)
{
    otaManager = new OTAUpdateManager();
#if !defined(MATRIX_PORTAL_M4)
    githubOTA = new GitHubOTA();
#endif
    instanceForCallback = this; // Set static instance for OTA callback
}

ConfigWebServer::~ConfigWebServer()
{
    stop();

    if (otaManager != nullptr)
    {
        delete otaManager;
        otaManager = nullptr;
    }

#if !defined(MATRIX_PORTAL_M4)
    if (githubOTA != nullptr)
    {
        delete githubOTA;
        githubOTA = nullptr;
    }
#endif
}

bool ConfigWebServer::begin()
{
    if (server != nullptr)
    {
        return true; // Already started
    }

    server = new WebServerType(80);

    // Register handlers with lambda wrappers to access 'this'
    server->on("/", HTTP_GET, [this]()
               { handleRoot(); });
    server->on("/save", HTTP_POST, [this]()
               { handleSave(); });
    server->on("/refresh", HTTP_POST, [this]()
               { handleRefresh(); });
    server->on("/reboot", HTTP_POST, [this]()
               { handleReboot(); });
    server->on("/clear-config", HTTP_POST, [this]()
               { handleClearConfig(); });
    server->on("/update", HTTP_GET, [this]()
               { handleUpdate(); });
#if !defined(MATRIX_PORTAL_M4)
    // OTA upload handlers - ESP32 only (uses Update library)
    server->on("/update", HTTP_POST,
               [this]() { handleUpdateComplete(); },  // Completion handler
               [this]() { handleUpdateProgress(); }   // Upload chunk handler
    );
    server->on("/check-update", HTTP_GET, [this]()
               { handleCheckUpdate(); });
    server->on("/download-update", HTTP_POST, [this]()
               { handleDownloadUpdate(); });
#endif
    server->on("/demo", HTTP_GET, [this]()
               { handleDemo(); });
    server->on("/start-demo", HTTP_POST, [this]()
               { handleStartDemo(); });
    server->on("/stop-demo", HTTP_POST, [this]()
               { handleStopDemo(); });
    server->on("/on", HTTP_GET, [this]()
               { handleScreenOn(); });
    server->on("/off", HTTP_GET, [this]()
               { handleScreenOff(); });
    server->onNotFound([this]()
                       { handleNotFound(); });

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

void ConfigWebServer::setCallbacks(ConfigSaveCallback onSave, RefreshCallback onRefresh, RebootCallback onReboot,
                                  DemoStartCallback onDemoStart, DemoStopCallback onDemoStop)
{
    onSaveCallback = onSave;
    onRefreshCallback = onRefresh;
    onRebootCallback = onReboot;
    onDemoStartCallback = onDemoStart;
    onDemoStopCallback = onDemoStop;
}

void ConfigWebServer::setDisplayManager(DisplayManager *displayMgr)
{
    displayManager = displayMgr;
}

void ConfigWebServer::updateState(const Config *config,
                                  bool connected, bool apMode,
                                  const char *ssid, const char *password, int clientCount,
                                  bool error, const char *errorMsg,
                                  int depCount, const char *stop)
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
        IPAddress ip = WiFi.localIP();
        char ipStr[16];
        snprintf(ipStr, sizeof(ipStr), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
        html += "<div class='status ok'>WiFi Connected: " + String(ipStr) + "</div>";
    }
    else
    {
        html += "<div class='status error'>WiFi Disconnected</div>";
    }

    if (!apModeActive)
    {
        // Display current city's configuration status
        bool isPrague = (strcmp(currentConfig->city, "Berlin") != 0);
        bool hasApiKey = isPrague ? (strlen(currentConfig->pragueApiKey) > 0) : true; // Berlin doesn't need API key
        bool hasStops = isPrague ? (strlen(currentConfig->pragueStopIds) > 0) : (strlen(currentConfig->berlinStopIds) > 0);

        if (hasApiKey && hasStops)
        {
            if (apiError)
            {
                html += "<div class='status error'>API Error: " + String(apiErrorMsg) + "</div>";
            }
            else
            {
                html += "<div class='status ok'>API OK - " + String(departureCount) + " departures</div>";
            }

            if (isPrague)
            {
                html += "<p><strong>Prague API Key:</strong> Configured (hidden)</p>";
            }
            else
            {
                html += "<p><strong>Berlin API:</strong> No authentication required</p>";
            }
        }
        else if (!hasApiKey && isPrague)
        {
            html += "<div class='status warn'>Prague API Key not configured</div>";
        }
        else if (!hasStops)
        {
            html += "<div class='status warn'>Stop IDs not configured</div>";
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

    // City selector
    html += "<label>Transit City</label>";
    html += "<select name='city' id='citySelect' onchange='switchCity()' required>";
    bool isPrague = (strcmp(currentConfig->city, "Berlin") != 0);
    html += "<option value='Prague'" + String(isPrague ? " selected" : "") + ">Prague (PID/Golemio)</option>";
    html += "<option value='Berlin'" + String(isPrague ? "" : " selected") + ">Berlin (BVG)</option>";
    html += "</select>";
    html += "<p class='info'>Select your transit network. Device will restart after changing city.</p>";

    // Hidden inputs to store per-city data
    html += "<input type='hidden' id='pragueApiKeyData' value='" + String(currentConfig->pragueApiKey) + "'>";
    html += "<input type='hidden' id='pragueStopsData' value='" + String(currentConfig->pragueStopIds) + "'>";
    html += "<input type='hidden' id='berlinStopsData' value='" + String(currentConfig->berlinStopIds) + "'>";

    // API Key field (Prague only)
    html += "<div id='apiKeySection'>";
    html += "<label><span id='apiKeyLabel'>Prague API Key (Golemio)</span></label>";
    bool hasPragueKey = strlen(currentConfig->pragueApiKey) > 0;
    // Show placeholder dots if API key exists, otherwise empty
    html += "<input type='password' name='apikey' id='apiKeyInput' placeholder='" + String(hasPragueKey ? "••••" : "Enter API key") + "' value=''>";
    if (apModeActive)
    {
        html += "<p class='info' id='apiKeyHelp'>Get your API key at <a href='https://api.golemio.cz/api-keys/' target='_blank'>api.golemio.cz</a>. Try the demo first!</p>";
    }
    else if (hasPragueKey)
    {
        html += "<p class='info' id='apiKeyHelp'>API key configured. Leave empty to keep current key, or enter a new key to replace it. Get keys at <a href='https://api.golemio.cz/api-keys/' target='_blank'>api.golemio.cz</a></p>";
    }
    else
    {
        html += "<p class='info' id='apiKeyHelp'>Required: Get your API key at <a href='https://api.golemio.cz/api-keys/' target='_blank'>api.golemio.cz</a></p>";
    }
    html += "</div>";

    html += "<label>Stop ID(s)</label>";
    html += "<input type='text' name='stops' id='stopsInput' value='" + String(isPrague ? currentConfig->pragueStopIds : currentConfig->berlinStopIds) + "' required placeholder='e.g., U693Z2P (Prague) or 900013102 (Berlin)'>";
    html += "<p class='info' id='stopHelp'>";
    if (isPrague)
    {
        html += "Comma-separated PID stop IDs (e.g., U693Z2P). Find IDs at <a href='https://data.pid.cz/stops/json/stops.json' target='_blank'>PID data</a>";
    }
    else
    {
        html += "Comma-separated numeric BVG stop IDs (e.g., 900013102). Find IDs at <a href='https://v6.bvg.transport.rest/' target='_blank'>BVG API</a>";
    }
    html += "</p>";

    html += "<div class='grid'>";
    html += "<div><label>Refresh Interval (sec)</label>";
    html += "<input type='number' name='refresh' value='" + String(currentConfig->refreshInterval) + "' min='10' max='300'></div>";

    html += "<div><label>Number of Departures to Display (1-3 rows)</label>";
    html += "<input type='number' name='numdeps' value='" + String(currentConfig->numDepartures) + "' min='1' max='3'></div>";

    html += "<div><label>Min Departure Time (min)</label>";
    html += "<input type='number' name='mindeptime' value='" + String(currentConfig->minDepartureTime) + "' min='0' max='30'></div>";

    html += "<div><label>Display Brightness (0-255)</label>";
    html += "<input type='number' name='brightness' value='" + String(currentConfig->brightness) + "' min='0' max='255'></div>";

    html += "<div style='margin-top:10px;'><label><input type='checkbox' name='debugmode' " + String(currentConfig->debugMode ? "checked" : "") + "> Enable Debug Mode (Telnet on port 23)</label></div>";
    html += "</div>";

    // Weather section (only show when not in AP mode)
    if (!apModeActive)
    {
        html += "<div class='card'>";
        html += "<h2>Weather Display</h2>";
        html += "<p class='info'>Show current weather conditions in the status bar. Uses Open-Meteo API (free, no key required).</p>";

        html += "<div style='margin-bottom:15px;'><label><input type='checkbox' name='weather_enabled' " + String(currentConfig->weatherEnabled ? "checked" : "") + "> Enable Weather Display</label></div>";

        html += "<div class='grid'>";
        html += "<div><label>Latitude</label>";
        html += "<input type='text' name='weather_lat' value='" + String(currentConfig->weatherLatitude, 6) + "' placeholder='e.g. 50.0755'></div>";

        html += "<div><label>Longitude</label>";
        html += "<input type='text' name='weather_lon' value='" + String(currentConfig->weatherLongitude, 6) + "' placeholder='e.g. 14.4378'></div>";
        html += "</div>";

        html += "<div><label>Refresh Interval (minutes)</label>";
        html += "<input type='number' name='weather_refresh' value='" + String(currentConfig->weatherRefreshInterval) + "' min='10' max='60'></div>";

        html += "<p class='info' style='font-size:0.9em; margin-top:10px;'>Find your coordinates at <a href='https://www.latlong.net/' target='_blank'>latlong.net</a></p>";
        html += "</div>";
    }

    // Line Colors section (only show when not in AP mode)
    if (!apModeActive)
    {
        html += "<div class='card'>";
        html += "<h2>Line Colors</h2>";
        html += "<p class='info'>Configure custom colors for specific transit lines. Leave empty to use defaults.</p>";
        html += "<p class='info' style='font-size:0.9em; color:#888;'>";
        html += "💡 <strong>Pattern matching:</strong> Use * as position placeholders<br>";
        html += "• <code>9*</code> = 2-digit lines (91-99)<br>";
        html += "• <code>95*</code> = 3-digit lines (950-959)<br>";
        html += "• <code>4**</code> = 3-digit lines (400-499)<br>";
        html += "• <code>C***</code> = 4-digit lines (C000-C999)<br>";
        html += "• Exact matches (e.g., \"A\", \"91\") take priority over patterns";
        html += "</p>";

        // Table header
        html += "<table id='lineColorTable' style='width:100%; margin-bottom:10px; border-collapse: collapse;'>";
        html += "<thead><tr style='border-bottom: 2px solid #444;'>";
        html += "<th style='text-align:left; padding:8px;'>Line</th>";
        html += "<th style='text-align:left; padding:8px;'>Color</th>";
        html += "<th style='text-align:center; padding:8px; width:60px;'>Action</th>";
        html += "</tr></thead>";
        html += "<tbody id='lineColorRows'>";

        // Parse existing config and create rows
        if (currentConfig && strlen(currentConfig->lineColorMap) > 0)
        {
            char mapCopy[256];
            strlcpy(mapCopy, currentConfig->lineColorMap, sizeof(mapCopy));

            char *token = strtok(mapCopy, ",");
            while (token != nullptr)
            {
                char *equals = strchr(token, '=');
                if (equals)
                {
                    *equals = '\0';
                    const char *lineName = token;
                    const char *colorName = equals + 1;

                    // Generate row with values
                    html += "<tr>";
                    html += "<td style='padding:8px;'>";
                    html += "<input type='text' class='lineInput' value='" + String(lineName) + "' ";
                    html += "style='width:80px; padding:5px;' maxlength='5' placeholder='A or 9*'>";
                    html += "</td>";
                    html += "<td style='padding:8px;'>";
                    html += "<select class='colorSelect' style='width:100%; padding:5px;'>";

                    // Color options (mark selected)
                    const char *colors[] = {"RED", "GREEN", "BLUE", "YELLOW", "ORANGE", "PURPLE", "CYAN", "WHITE"};
                    for (int i = 0; i < 8; i++)
                    {
                        html += "<option value='" + String(colors[i]) + "'";
                        if (strcasecmp(colorName, colors[i]) == 0)
                        {
                            html += " selected";
                        }
                        html += ">" + String(colors[i]) + "</option>";
                    }

                    html += "</select>";
                    html += "</td>";
                    html += "<td style='padding:8px; text-align:center;'>";
                    html += "<button type='button' onclick='deleteLineRow(this)' ";
                    html += "style='background:#ff6b6b; color:#fff; padding:5px 10px; border:none; cursor:pointer;'>✕</button>";
                    html += "</td>";
                    html += "</tr>";
                }
                token = strtok(nullptr, ",");
            }
        }

        html += "</tbody>";
        html += "</table>";

        // Add Row button
        html += "<button type='button' onclick='addLineRow()' ";
        html += "style='background:#00d4ff; color:#fff; padding:8px 15px; border:none; cursor:pointer; margin-bottom:10px;'>";
        html += "+ Add Line";
        html += "</button>";

        // Hidden input to store serialized data
        html += "<input type='hidden' name='linecolormap' id='lineColorMapData' value=''>";

        html += "</div>";
    }

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
        html += "<form method='GET' action='/demo' style='display:inline; margin-top:10px'>";
        html += "<button type='submit' style='background:#9b59b6;'>Display Demo</button>";
        html += "</form>";
        html += "<form method='GET' action='/update' style='display:inline; margin-top:10px'>";
        html += "<button type='submit'>Install Firmware</button>";
        html += "</form>";
        html += "<form id='checkUpdateForm' onsubmit='checkForUpdate(event); return false;' style='display:inline; margin-top:10px'>";
        html += "<button type='submit' id='checkUpdateBtn'>Check for Updates</button>";
        html += "</form>";
        html += "<form method='POST' action='/reboot' style='display:inline; margin-top:10px'>";
        html += "<button type='submit' class='danger'>Reboot Device</button>";
        html += "</form>";
        html += "<form method='POST' action='/clear-config' onsubmit='return confirm(\"⚠️ WARNING: This will erase ALL settings and reboot into setup mode. Continue?\");' style='display:inline; margin-top:10px'>";
        html += "<button type='submit' class='danger'>Reset All Settings</button>";
        html += "</form>";
        html += "<div id='updateStatus' style='display:none; margin-top:15px;'></div>";
        html += "</div>";
    }
    else
    {
        // Demo is available in AP mode
        html += "<div class='card'>";
        html += "<h2>Demo</h2>";
        html += "<p>Try out the display with sample departure data before configuring API access.</p>";
        html += "<form method='GET' action='/demo' style='display:inline'>";
        html += "<button type='submit' style='background:#9b59b6;'>View Display Demo</button>";
        html += "</form>";
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
                    <p style='margin-top:20px;'>Please wait 15-20 seconds for device to restart.</p>
                </div>
            `;
            setTimeout(() => {
                status.innerHTML += `
                    <div style='margin-top:20px;'>
                        <button onclick='window.location.reload()' style='padding:12px 24px; font-size:16px; cursor:pointer; background:#2ed573; color:#000; border:none; border-radius:8px;'>🔌 Reconnect to Device</button>
                    </div>
                `;
            }, 15000);
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

    // JavaScript for city switching (always included)
    html += R"rawliteral(
<script>
// Track the currently displayed city (starts with server-provided value)
let currentDisplayedCity = document.getElementById('citySelect').value;

// City switching logic - dynamically show/hide and update fields
function switchCity() {
    const newCity = document.getElementById('citySelect').value;
    const apiKeySection = document.getElementById('apiKeySection');
    const apiKeyInput = document.getElementById('apiKeyInput');
    const stopsInput = document.getElementById('stopsInput');
    const stopHelp = document.getElementById('stopHelp');

    // Save current visible stopIds to hidden field BEFORE switching
    // (currentDisplayedCity contains the city we're switching FROM)
    if (currentDisplayedCity === 'Prague') {
        // Currently showing Prague, save Prague stops
        document.getElementById('pragueStopsData').value = stopsInput.value;
    } else {
        // Currently showing Berlin, save Berlin stops
        document.getElementById('berlinStopsData').value = stopsInput.value;
    }

    // Now load the new city's data
    if (newCity === 'Prague') {
        // Show API key for Prague
        apiKeySection.style.display = 'block';

        // Load Prague stops from hidden field
        stopsInput.value = document.getElementById('pragueStopsData').value;
        stopsInput.placeholder = 'e.g., U693Z2P';

        // Reset API key input field (don't expose saved key, just show placeholder)
        const pragueApiKey = document.getElementById('pragueApiKeyData').value;
        apiKeyInput.value = '';
        apiKeyInput.placeholder = pragueApiKey.length > 0 ? '••••' : 'Enter API key';

        // Update help text
        stopHelp.innerHTML = 'Comma-separated PID stop IDs (e.g., U693Z2P). Find IDs at <a href="https://data.pid.cz/stops/json/stops.json" target="_blank">PID data</a>';
    } else {
        // Hide API key for Berlin
        apiKeySection.style.display = 'none';

        // Load Berlin stops from hidden field
        stopsInput.value = document.getElementById('berlinStopsData').value;
        stopsInput.placeholder = 'e.g., 900013102';

        // Update help text
        stopHelp.innerHTML = 'Comma-separated numeric BVG stop IDs (e.g., 900013102). Find IDs at <a href="https://v6.bvg.transport.rest/" target="_blank">BVG API</a>';
    }

    // Update the tracked city to the new one
    currentDisplayedCity = newCity;
}

// Initialize city-specific display on page load
document.addEventListener('DOMContentLoaded', function() {
    switchCity();
});
</script>
)rawliteral";

    // JavaScript for line color configuration
    if (!apModeActive)
    {
        html += R"rawliteral(
<script>
// Add new empty row to line color table
function addLineRow() {
    const tbody = document.getElementById('lineColorRows');
    const row = tbody.insertRow();

    // Line input cell
    const cell1 = row.insertCell(0);
    cell1.style.padding = '8px';
    cell1.innerHTML = "<input type='text' class='lineInput' style='width:80px; padding:5px;' maxlength='5' placeholder='A or 9*'>";

    // Color select cell
    const cell2 = row.insertCell(1);
    cell2.style.padding = '8px';
    const colors = ['RED', 'GREEN', 'BLUE', 'YELLOW', 'ORANGE', 'PURPLE', 'CYAN', 'WHITE'];
    let selectHtml = "<select class='colorSelect' style='width:100%; padding:5px;'>";
    colors.forEach(color => {
        selectHtml += `<option value='${color}'>${color}</option>`;
    });
    selectHtml += "</select>";
    cell2.innerHTML = selectHtml;

    // Delete button cell
    const cell3 = row.insertCell(2);
    cell3.style.padding = '8px';
    cell3.style.textAlign = 'center';
    cell3.innerHTML = "<button type='button' onclick='deleteLineRow(this)' style='background:#ff6b6b; color:#fff; padding:5px 10px; border:none; cursor:pointer;'>✕</button>";
}

// Delete row from table
function deleteLineRow(btn) {
    const row = btn.closest('tr');
    row.remove();
}

// Serialize table to hidden input before form submit
function serializeLineColors() {
    const rows = document.querySelectorAll('#lineColorRows tr');
    const mappings = [];

    rows.forEach(row => {
        const lineInput = row.querySelector('.lineInput');
        const colorSelect = row.querySelector('.colorSelect');

        if (lineInput && colorSelect) {
            const line = lineInput.value.trim().toUpperCase();
            const color = colorSelect.value;

            // Only include non-empty line names
            if (line.length > 0) {
                mappings.push(`${line}=${color}`);
            }
        }
    });

    // Store as comma-separated string
    document.getElementById('lineColorMapData').value = mappings.join(',');

    return true;  // Allow form submission
}

// Attach serializer to form submit
document.querySelector('form').addEventListener('submit', function(e) {
    serializeLineColors();
});
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
    bool cityChanged = false;

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
    // Parse city field
    if (server->hasArg("city"))
    {
        String newCity = server->arg("city");
        // Validate city value
        if (newCity == "Berlin" || newCity == "Prague")
        {
            if (newCity != newConfig.city)
            {
                cityChanged = true;
            }
            strlcpy(newConfig.city, newCity.c_str(), sizeof(newConfig.city));
        }
        else
        {
            // Invalid city value, default to Prague
            strlcpy(newConfig.city, "Prague", sizeof(newConfig.city));
        }
    }
    // Save API key and stops to appropriate city-specific fields
    String selectedCity = String(newConfig.city);

    if (server->hasArg("apikey") && server->arg("apikey").length() > 0)
    {
        String apiKeyValue = server->arg("apikey");
        // Only save if it's not the placeholder dots (visual feedback, not actual key)
        if (apiKeyValue != "••••" && selectedCity == "Prague")
        {
            strlcpy(newConfig.pragueApiKey, apiKeyValue.c_str(), sizeof(newConfig.pragueApiKey));
        }
    }

    if (server->hasArg("stops"))
    {
        String stops = server->arg("stops");

        // Validate maximum number of stops (with 1s delay per stop, 12 stops = 12s+ query time)
        int numStops = countStops(stops.c_str());
        if (numStops > 12)
        {
            server->send(400, "text/plain",
                "Error: Too many stops configured (max 12). Please reduce the number of stops.\n"
                "With 1-second delay between API calls, 12 stops takes 12+ seconds to query.");
            logTimestamp();
            debugPrintln("Config save failed: too many stops");
            return;
        }

        // Save to city-specific field
        if (selectedCity == "Prague")
        {
            strlcpy(newConfig.pragueStopIds, stops.c_str(), sizeof(newConfig.pragueStopIds));
        }
        else if (selectedCity == "Berlin")
        {
            strlcpy(newConfig.berlinStopIds, stops.c_str(), sizeof(newConfig.berlinStopIds));
        }
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
        if (newConfig.numDepartures > 3)
            newConfig.numDepartures = 3;  // Max 3 rows on display
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

    // Debug mode checkbox (unchecked = not present in POST data)
    newConfig.debugMode = server->hasArg("debugmode");

    // Weather configuration
    newConfig.weatherEnabled = server->hasArg("weather_enabled");

    if (server->hasArg("weather_lat"))
    {
        String latStr = server->arg("weather_lat");
        // Replace comma with dot for decimal separator (locale compatibility)
        latStr.replace(",", ".");
        newConfig.weatherLatitude = latStr.toFloat();
        // Validate latitude range
        if (newConfig.weatherLatitude < -90.0f)
            newConfig.weatherLatitude = -90.0f;
        if (newConfig.weatherLatitude > 90.0f)
            newConfig.weatherLatitude = 90.0f;
    }

    if (server->hasArg("weather_lon"))
    {
        String lonStr = server->arg("weather_lon");
        // Replace comma with dot for decimal separator (locale compatibility)
        lonStr.replace(",", ".");
        newConfig.weatherLongitude = lonStr.toFloat();
        // Validate longitude range
        if (newConfig.weatherLongitude < -180.0f)
            newConfig.weatherLongitude = -180.0f;
        if (newConfig.weatherLongitude > 180.0f)
            newConfig.weatherLongitude = 180.0f;
    }

    if (server->hasArg("weather_refresh"))
    {
        newConfig.weatherRefreshInterval = server->arg("weather_refresh").toInt();
        // Clamp to 10-60 minutes
        if (newConfig.weatherRefreshInterval < 10)
            newConfig.weatherRefreshInterval = 10;
        if (newConfig.weatherRefreshInterval > 60)
            newConfig.weatherRefreshInterval = 60;
    }

    // Line color map (always update when not in AP mode to handle empty case)
    if (!apModeActive)
    {
        // Get the value, defaulting to empty string if not present
        String colorMapValue = server->hasArg("linecolormap")
                               ? server->arg("linecolormap")
                               : "";

        strlcpy(newConfig.lineColorMap, colorMapValue.c_str(), sizeof(newConfig.lineColorMap));

        // Log configuration
        logTimestamp();
        Serial.print("Line color map updated: ");
        Serial.println(strlen(newConfig.lineColorMap) > 0 ? newConfig.lineColorMap : "(empty - using defaults)");
    }

    newConfig.configured = true;

    // If in AP mode, WiFi changed, or city changed, show restart message
    if (apModeActive || wifiChanged || cityChanged)
    {
        String html = HTML_HEADER;
        html += "<h1>⏳ Restarting...</h1>";
        if (cityChanged)
        {
            html += "<p>Transit city changed to: <strong>" + String(newConfig.city) + "</strong></p>";
            html += "<p>The device will restart to apply the new transit API configuration.</p>";
            html += "<p>Please wait 10-15 seconds for it to come back online.</p>";
        }
        else
        {
            html += "<p>Attempting to connect to WiFi network: <strong>" + String(newConfig.wifiSsid) + "</strong></p>";
            html += "<p>Please wait... The device will restart and connect to the new network.</p>";
            html += "<p>If connection fails, the device will return to AP mode.</p>";
        }
        html += "<div class='card'>";
        html += "<p>After successful restart, access the device at its IP address.</p>";
        html += "</div>";
        html += "<div id='reconnect-msg' style='display:none; margin-top:20px;'>";
        html += "<p><button onclick='window.location=\"/\"' style='padding:12px 24px; font-size:16px; cursor:pointer; background:#2ed573; color:#000; border:none; border-radius:8px;'>🔌 Reconnect to Device</button></p>";
        html += "</div>";
        html += "<script>setTimeout(function(){ document.getElementById('reconnect-msg').style.display='block'; }, 10000);</script>";
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
    // Pass true for restart if either WiFi or city changed
    onSaveCallback(newConfig, wifiChanged || cityChanged);
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
    html += "<p>The device is rebooting. Please wait 10-15 seconds for it to come back online.</p>";
    html += "<div id='reconnect-msg' style='display:none; margin-top:20px;'>";
    html += "<p><button onclick='window.location=\"/\"' style='padding:12px 24px; font-size:16px; cursor:pointer; background:#2ed573; color:#000; border:none; border-radius:8px;'>🔌 Reconnect to Device</button></p>";
    html += "</div>";
    html += "<script>setTimeout(function(){ document.getElementById('reconnect-msg').style.display='block'; }, 10000);</script>";
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
    html += "<p>The device will reboot into AP (setup) mode in 10 seconds.</p>";
    html += "<p>You will need to reconfigure WiFi and API settings.</p>";
    html += "</div>";
    html += "<div id='reconnect-msg' style='display:none; margin-top:20px;'>";
    html += "<p><strong>Device should now be in AP mode.</strong></p>";
    html += "<p>Look for a WiFi network starting with: <strong>SpojBoard-XXXX</strong></p>";
    html += "</div>";
    html += "<script>setTimeout(function(){ document.getElementById('reconnect-msg').style.display='block'; }, 15000);</script>";
    html += HTML_FOOTER;
    server->send(200, "text/html", html);

    // Clear all config from NVS
    clearConfig();

    // Reboot after a short delay
    delay(10000);
    systemRestart();
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

void ConfigWebServer::handleUpdateProgress()
{
    // This function is called during upload to process chunks
    // It should NOT send any HTTP responses

    if (otaManager == nullptr)
    {
        return;
    }

    // Block uploads in AP mode
    if (apModeActive)
    {
        return;
    }

    // Let OTA manager handle the upload with progress callback
    otaManager->handleUpload(server, otaProgressCallback);
}

void ConfigWebServer::handleUpdateComplete()
{
    // This function is called once after upload completes
    // It sends the final HTTP response

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

    // Check if upload succeeded or failed
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
        html += "<p>The device will reboot in 10 seconds. Please wait 15-20 seconds for it to come back online.</p>";
        html += "</div>";
        html += "<div id='reconnect-msg' style='display:none; margin-top:20px;'>";
        html += "<p><button onclick='window.location=\"/\"' style='padding:12px 24px; font-size:16px; cursor:pointer; background:#2ed573; color:#000; border:none; border-radius:8px;'>🔌 Reconnect to Device</button></p>";
        html += "</div>";
        html += "<script>setTimeout(function(){ document.getElementById('reconnect-msg').style.display='block'; }, 15000);</script>";
        html += HTML_FOOTER;
        server->send(200, "text/html", html);

        // Reboot after a short delay
        delay(10000);
        systemRestart();
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
String escapeJsonString(const char *str)
{
    String result = "";
    if (!str)
        return result;

    for (size_t i = 0; str[i] != '\0'; i++)
    {
        char c = str[i];
        switch (c)
        {
        case '"':
            result += "\\\"";
            break;
        case '\\':
            result += "\\\\";
            break;
        case '\n':
            result += "\\n";
            break;
        case '\r':
            result += "\\r";
            break;
        case '\t':
            result += "\\t";
            break;
        case '\b':
            result += "\\b";
            break;
        case '\f':
            result += "\\f";
            break;
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

#if !defined(MATRIX_PORTAL_M4)
// GitHub OTA functions - ESP32 only

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
    if (sizeEnd < 0)
        sizeEnd = body.indexOf("}", sizeStart);
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
        Serial.println("Update successful, rebooting in 10 seconds...");

        delay(10000);
        systemRestart();
    }
    else
    {
        server->send(500, "application/json", "{\"success\":false,\"error\":\"Download or installation failed\"}");
    }
}

#endif // !MATRIX_PORTAL_M4

void ConfigWebServer::handleDemo()
{
    String html = HTML_HEADER;
    html += "<h1>🎨 Display Demo</h1>";
    html += "<p style='text-align:center; color:#888; margin-top:-10px; margin-bottom:20px;'>Preview and customize the LED display</p>";

    // Demo configuration card
    html += "<div class='card'>";
    html += "<h2>Sample Departures</h2>";
    html += "<p class='info'>Edit the sample data below to preview different line colors, destinations, and ETAs on your LED matrix display.</p>";

    // Demo form
    html += "<form id='demoForm' onsubmit='startDemo(event); return false;'>";

    // Sample departures (3 rows)
    for (int i = 1; i <= 3; i++)
    {
        html += "<div style='border: 1px solid #333; padding: 15px; margin: 10px 0; border-radius: 5px;'>";
        html += "<h3 style='color: #00d4ff; margin-top: 0;'>Departure " + String(i) + "</h3>";
        html += "<div class='grid'>";

        html += "<div><label>Line Number</label>";
        html += "<input type='text' name='line" + String(i) + "' value='" + (i == 1 ? "12" : (i == 2 ? "C" : "S9")) + "' maxlength='7' required></div>";

        html += "<div><label>Destination</label>";
        html += "<input type='text' name='dest" + String(i) + "' value='" +
                (i == 1 ? "Štvanice" : (i == 2 ? "Nádr. Holešovice" : "Praha-Eden")) +
                "' maxlength='31' required></div>";

        html += "<div><label>ETA (minutes)</label>";
        html += "<input type='number' name='eta" + String(i) + "' value='" + String(i * 2) + "' min='0' max='120' required></div>";

        html += "<div style='margin-top:10px;'><label><input type='checkbox' name='ac" + String(i) + "' " +
                (i == 1 ? "checked" : "") + "> Air Conditioned</label></div>";

        html += "</div></div>";
    }

    html += "<button type='submit' style='background:#9b59b6; margin-top:20px;'>▶ Start Demo</button>";
    html += "</form>";
    html += "</div>";

    // Status card
    html += "<div class='card'>";
    html += "<h2>Demo Status</h2>";
    html += "<div id='demoStatus'>";
    html += "<p style='color:#888;'>Demo not running. Click \"Start Demo\" above to preview on the LED display.</p>";
    html += "</div>";
    html += "<form method='POST' action='/stop-demo' id='stopDemoForm' style='display:none;'>";
    html += "<button type='submit' class='danger'>⏹ Stop Demo & Resume Normal Operation</button>";
    html += "</form>";
    html += "</div>";

    // Info card
    html += "<div class='card' style='background: #2e3b4e;'>";
    html += "<h3 style='color: #00d4ff; margin-top: 0;'>ℹ️ About Demo Mode</h3>";
    html += "<ul style='margin: 10px 0; padding-left: 20px; line-height: 1.6;'>";
    html += "<li>Demo mode displays your custom sample data on the LED matrix</li>";
    html += "<li>While demo is running, API polling and automatic time updates are paused</li>";
    html += "<li>You can click \"Start Demo\" repeatedly to test different configurations</li>";
    html += "<li>Stop demo mode or reboot device to resume normal operation</li>";
    html += "<li>Demo is available in both AP mode (setup) and STA mode (connected)</li>";
    html += "</ul>";
    html += "</div>";

    html += "<p><a href='/'>← Back to Dashboard</a></p>";

    // JavaScript for demo control
    html += R"rawliteral(
<script>
async function startDemo(event) {
    event.preventDefault();
    const form = document.getElementById('demoForm');
    const formData = new FormData(form);

    // Build JSON payload
    const departures = [];
    for (let i = 1; i <= 3; i++) {
        departures.push({
            line: formData.get('line' + i),
            destination: formData.get('dest' + i),
            eta: parseInt(formData.get('eta' + i)),
            hasAC: formData.has('ac' + i)
        });
    }

    try {
        const response = await fetch('/start-demo', {
            method: 'POST',
            headers: {'Content-Type': 'application/json'},
            body: JSON.stringify({ departures: departures })
        });

        const data = await response.json();

        if (data.success) {
            document.getElementById('demoStatus').innerHTML = `
                <div class='status ok'>
                    ✅ Demo mode active! Check your LED display.
                    <p style='margin-top:10px; color:#000;'>The display is now showing your sample departure data. API polling and time updates are paused.</p>
                </div>
            `;
            document.getElementById('stopDemoForm').style.display = 'block';
        } else {
            document.getElementById('demoStatus').innerHTML = `
                <div class='status error'>❌ Failed to start demo: ${data.error}</div>
            `;
        }
    } catch (error) {
        document.getElementById('demoStatus').innerHTML = `
            <div class='status error'>❌ Error: ${error.message}</div>
        `;
    }
}
</script>
)rawliteral";

    html += HTML_FOOTER;
    server->send(200, "text/html", html);
}

void ConfigWebServer::handleStartDemo()
{
    // Parse JSON request body
    String body = server->arg("plain");

    // Simple JSON parsing for departures array
    Departure demoDepartures[3];
    int demoCount = 0;

    // Extract departure data from JSON (manual parsing for simplicity)
    for (int i = 0; i < 3; i++)
    {
        // Find "line":"<value>" pattern
        String lineKey = "\"line\":\"";
        int lineStart = body.indexOf(lineKey, 0);
        if (lineStart < 0) break;
        lineStart += lineKey.length();
        int lineEnd = body.indexOf("\"", lineStart);
        String lineValue = body.substring(lineStart, lineEnd);

        // Find "destination":"<value>" pattern
        String destKey = "\"destination\":\"";
        int destStart = body.indexOf(destKey, lineEnd);
        if (destStart < 0) break;
        destStart += destKey.length();
        int destEnd = body.indexOf("\"", destStart);
        String destValue = body.substring(destStart, destEnd);

        // Find "eta":<value> pattern
        String etaKey = "\"eta\":";
        int etaStart = body.indexOf(etaKey, destEnd);
        if (etaStart < 0) break;
        etaStart += etaKey.length();
        int etaEnd = body.indexOf(",", etaStart);
        if (etaEnd < 0) etaEnd = body.indexOf("}", etaStart);
        String etaValue = body.substring(etaStart, etaEnd);

        // Find "hasAC":<value> pattern
        String acKey = "\"hasAC\":";
        int acStart = body.indexOf(acKey, etaEnd);
        bool hasAC = false;
        if (acStart >= 0)
        {
            acStart += acKey.length();
            int acEnd = body.indexOf(",", acStart);
            if (acEnd < 0) acEnd = body.indexOf("}", acStart);
            String acValue = body.substring(acStart, acEnd);
            hasAC = acValue.indexOf("true") >= 0;
        }

        // Copy to departure structure
        strlcpy(demoDepartures[demoCount].line, lineValue.c_str(), sizeof(demoDepartures[demoCount].line));
        strlcpy(demoDepartures[demoCount].destination, destValue.c_str(), sizeof(demoDepartures[demoCount].destination));
        demoDepartures[demoCount].eta = etaValue.toInt();
        demoDepartures[demoCount].hasAC = hasAC;
        demoDepartures[demoCount].isDelayed = false;
        demoDepartures[demoCount].delayMinutes = 0;
        demoDepartures[demoCount].departureTime = 0;  // Not used in demo mode

        demoCount++;

        // Move search position forward for next departure
        body = body.substring(etaEnd + 1);
    }

    if (demoCount == 0)
    {
        server->send(400, "application/json", "{\"success\":false,\"error\":\"No departure data found\"}");
        return;
    }

    // Call callback to activate demo mode
    if (onDemoStartCallback != nullptr)
    {
        onDemoStartCallback(demoDepartures, demoCount);
    }

    // Show demo on display immediately
    if (displayManager != nullptr)
    {
        displayManager->drawDemo(demoDepartures, demoCount, "Demo Mode");
    }

    logTimestamp();
    Serial.print("Demo mode started with ");
    Serial.print(demoCount);
    Serial.println(" departures");

    server->send(200, "application/json", "{\"success\":true,\"message\":\"Demo mode activated\"}");
}

void ConfigWebServer::handleStopDemo()
{
    // Call callback to deactivate demo mode
    if (onDemoStopCallback != nullptr)
    {
        onDemoStopCallback();
    }

    logTimestamp();
    Serial.println("Demo mode stopped");

    server->sendHeader("Location", "/");
    server->send(302, "text/plain", "");
}

void ConfigWebServer::handleScreenOn()
{
    if (displayManager != nullptr)
    {
        displayManager->turnOn();
    }
    server->send(200, "text/plain", "OK");
}

void ConfigWebServer::handleScreenOff()
{
    if (displayManager != nullptr)
    {
        displayManager->turnOff();
    }
    server->send(200, "text/plain", "OK");
}

#endif // !MATRIX_PORTAL_M4
