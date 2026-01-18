#include "TimeUtils.h"
#include "Logger.h"
#include <Arduino.h>

#if defined(MATRIX_PORTAL_M4)
// M4 uses NTPClient library with WiFiNINA
#include <WiFiNINA.h>
#include <WiFiUdp.h>
#include <NTPClient.h>

static WiFiUDP ntpUDP;
// Use only CET offset (3600) - no DST in winter. TODO: proper DST handling
static NTPClient timeClient(ntpUDP, NTP_SERVER, GMT_OFFSET_SEC);
static bool timeInitialized = false;
#endif

void initTimeSync()
{
#if defined(MATRIX_PORTAL_M4)
    timeClient.begin();
#else
    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
#endif
}

bool syncTime(int maxAttempts, int delayMs)
{
    logTimestamp();
    Serial.println("Syncing time...");

#if defined(MATRIX_PORTAL_M4)
    int attempts = 0;
    while (!timeClient.update() && attempts < maxAttempts)
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

    timeInitialized = true;

    // Log the synced time
    logTimestamp();
    Serial.print("Time synced: ");
    Serial.println(timeClient.getFormattedTime());

    return true;
#else
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
#endif
}

bool getFormattedTime(char* buffer, size_t size, const char* format)
{
#if defined(MATRIX_PORTAL_M4)
    if (!timeInitialized)
    {
        return false;
    }

    // Update NTP client to get fresh time
    timeClient.update();

    // Convert epoch time to tm struct
    time_t epochTime = timeClient.getEpochTime();
    struct tm* timeinfo = localtime(&epochTime);

    if (timeinfo == nullptr)
    {
        return false;
    }

    strftime(buffer, size, format, timeinfo);
    return true;
#else
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo))
    {
        return false;
    }

    strftime(buffer, size, format, &timeinfo);
    return true;
#endif
}

bool getCurrentTime(struct tm* timeinfo)
{
#if defined(MATRIX_PORTAL_M4)
    if (!timeInitialized)
    {
        return false;
    }

    timeClient.update();
    time_t epochTime = timeClient.getEpochTime();
    struct tm* t = localtime(&epochTime);

    if (t == nullptr)
    {
        return false;
    }

    *timeinfo = *t;
    return true;
#else
    return getLocalTime(timeinfo);
#endif
}

time_t getCurrentEpochTime()
{
#if defined(MATRIX_PORTAL_M4)
    if (!timeInitialized)
    {
        return 0;
    }
    timeClient.update();
    return timeClient.getEpochTime();
#else
    time_t now;
    time(&now);
    return now;
#endif
}
