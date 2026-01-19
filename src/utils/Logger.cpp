#include "Logger.h"
#include "TelnetLogger.h"
#include "../config/AppConfig.h"

// Platform-specific includes for network diagnostics
#if defined(MATRIX_PORTAL_M4)
    #include <WiFiNINA.h>
#else
    #include <WiFi.h>
    #include <HTTPClient.h>
#endif

// Global config pointer for debug checks
static const Config* g_config = nullptr;

void initLogger(const Config* cfg)
{
    g_config = cfg;
}

void logTimestamp()
{
    char timestamp[24];
    sprintf(timestamp, "[%010lu] ", millis());
    Serial.print(timestamp);

    // Mirror to telnet if debug enabled AND telnet is active
    if (g_config && g_config->debugMode && TelnetLogger::getInstance().isActive())
    {
        TelnetLogger::getInstance().print(timestamp);
    }
}

void logMemory(const char *location)
{
    logTimestamp();
    Serial.print("MEM@");
    Serial.print(location);
#if defined(MATRIX_PORTAL_M4)
    // M4 doesn't have heap monitoring
    Serial.println(": (not available on M4)");
#else
    Serial.print(": Free=");
    Serial.print(ESP.getFreeHeap());
    Serial.print(" Min=");
    Serial.println(ESP.getMinFreeHeap());

    // Mirror to telnet if debug enabled AND telnet is active
    if (g_config && g_config->debugMode && TelnetLogger::getInstance().isActive())
    {
        char buf[128];
        snprintf(buf, sizeof(buf), "MEM@%s: Free=%u Min=%u\n",
                 location, ESP.getFreeHeap(), ESP.getMinFreeHeap());
        TelnetLogger::getInstance().print(buf);
    }
#endif
}

void debugPrint(const char* message)
{
    // Always print to Serial
    Serial.print(message);

    // Mirror to telnet ONLY if debug mode enabled AND telnet is active
    if (g_config && g_config->debugMode && TelnetLogger::getInstance().isActive())
    {
        TelnetLogger::getInstance().print(message);
    }
}

void debugPrintln(const char* message)
{
    // Always print to Serial
    Serial.println(message);

    // Mirror to telnet ONLY if debug mode enabled AND telnet is active
    if (g_config && g_config->debugMode && TelnetLogger::getInstance().isActive())
    {
        TelnetLogger::getInstance().println(message);
    }
}

const char* httpErrorToString(int httpCode)
{
    // Positive codes are HTTP status codes
    if (httpCode > 0)
    {
        switch (httpCode)
        {
            case 200: return "OK";
            case 201: return "Created";
            case 204: return "No Content";
            case 301: return "Moved Permanently";
            case 302: return "Found";
            case 304: return "Not Modified";
            case 400: return "Bad Request";
            case 401: return "Unauthorized";
            case 403: return "Forbidden";
            case 404: return "Not Found";
            case 408: return "Request Timeout";
            case 429: return "Too Many Requests";
            case 500: return "Internal Server Error";
            case 502: return "Bad Gateway";
            case 503: return "Service Unavailable";
            case 504: return "Gateway Timeout";
            default: return "HTTP Error";
        }
    }

    // Negative codes are ESP32 HTTPClient error codes
#if !defined(MATRIX_PORTAL_M4)
    switch (httpCode)
    {
        case HTTPC_ERROR_CONNECTION_REFUSED: return "CONNECTION_REFUSED";
        case HTTPC_ERROR_SEND_HEADER_FAILED: return "SEND_HEADER_FAILED";
        case HTTPC_ERROR_SEND_PAYLOAD_FAILED: return "SEND_PAYLOAD_FAILED";
        case HTTPC_ERROR_NOT_CONNECTED: return "NOT_CONNECTED";
        case HTTPC_ERROR_CONNECTION_LOST: return "CONNECTION_LOST";
        case HTTPC_ERROR_NO_STREAM: return "NO_STREAM";
        case HTTPC_ERROR_NO_HTTP_SERVER: return "NO_HTTP_SERVER";
        case HTTPC_ERROR_TOO_LESS_RAM: return "TOO_LESS_RAM";
        case HTTPC_ERROR_ENCODING: return "ENCODING_ERROR";
        case HTTPC_ERROR_STREAM_WRITE: return "STREAM_WRITE";
        case HTTPC_ERROR_READ_TIMEOUT: return "READ_TIMEOUT";
        default: return "UNKNOWN_ERROR";
    }
#else
    return "ERROR";
#endif
}

void logNetworkDiagnostics()
{
    char buf[128];

#if defined(MATRIX_PORTAL_M4)
    // M4 with WiFiNINA
    int status = WiFi.status();
    const char* statusStr = "UNKNOWN";
    switch (status)
    {
        case WL_CONNECTED: statusStr = "CONNECTED"; break;
        case WL_NO_SHIELD: statusStr = "NO_SHIELD"; break;
        case WL_IDLE_STATUS: statusStr = "IDLE"; break;
        case WL_NO_SSID_AVAIL: statusStr = "NO_SSID"; break;
        case WL_SCAN_COMPLETED: statusStr = "SCAN_DONE"; break;
        case WL_CONNECT_FAILED: statusStr = "CONNECT_FAILED"; break;
        case WL_CONNECTION_LOST: statusStr = "CONNECTION_LOST"; break;
        case WL_DISCONNECTED: statusStr = "DISCONNECTED"; break;
    }

    long rssi = WiFi.RSSI();
    const char* rssiQuality = rssi > -50 ? "excellent" : rssi > -60 ? "good" : rssi > -70 ? "fair" : "weak";

    snprintf(buf, sizeof(buf), "NET: WiFi=%s RSSI=%lddBm(%s)",
             statusStr, rssi, rssiQuality);
#else
    // ESP32
    int status = WiFi.status();
    const char* statusStr = "UNKNOWN";
    switch (status)
    {
        case WL_CONNECTED: statusStr = "CONNECTED"; break;
        case WL_NO_SHIELD: statusStr = "NO_SHIELD"; break;
        case WL_IDLE_STATUS: statusStr = "IDLE"; break;
        case WL_NO_SSID_AVAIL: statusStr = "NO_SSID"; break;
        case WL_SCAN_COMPLETED: statusStr = "SCAN_DONE"; break;
        case WL_CONNECT_FAILED: statusStr = "CONNECT_FAILED"; break;
        case WL_CONNECTION_LOST: statusStr = "CONNECTION_LOST"; break;
        case WL_DISCONNECTED: statusStr = "DISCONNECTED"; break;
    }

    long rssi = WiFi.RSSI();
    const char* rssiQuality = rssi > -50 ? "excellent" : rssi > -60 ? "good" : rssi > -70 ? "fair" : "weak";

    snprintf(buf, sizeof(buf), "NET: WiFi=%s RSSI=%lddBm(%s) Heap=%u/%u",
             statusStr, rssi, rssiQuality,
             ESP.getFreeHeap(), ESP.getMinFreeHeap());
#endif

    logTimestamp();
    debugPrintln(buf);
}
