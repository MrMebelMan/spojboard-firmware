#include "TimeUtils.h"
#include "Logger.h"
#include <Arduino.h>

void initTimeSync()
{
    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
}

bool syncTime(int maxAttempts, int delayMs)
{
    logTimestamp();
    Serial.println("Syncing time...");

    struct tm timeinfo;
    int attempts = 0;

    while (!getLocalTime(&timeinfo) && attempts < maxAttempts)
    {
        delay(delayMs);
        attempts++;
    }

    if (attempts >= maxAttempts)
    {
        logTimestamp();
        Serial.println("Time sync failed!");
        return false;
    }

    char timeStr[32];
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
    logTimestamp();
    Serial.print("Time synced: ");
    Serial.println(timeStr);

    return true;
}

bool getFormattedTime(char* buffer, size_t size, const char* format)
{
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo))
    {
        return false;
    }

    strftime(buffer, size, format, &timeinfo);
    return true;
}

bool getCurrentTime(struct tm* timeinfo)
{
    return getLocalTime(timeinfo);
}
