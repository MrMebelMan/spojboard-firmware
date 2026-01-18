#include "Logger.h"
#include "TelnetLogger.h"
#include "../config/AppConfig.h"

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
