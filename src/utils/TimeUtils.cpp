#include "TimeUtils.h"
#include "Logger.h"
#include <Arduino.h>

#if defined(MATRIX_PORTAL_M4)
// M4 uses NTPClient library with WiFiNINA
#include <WiFiNINA.h>
#include <WiFiUdp.h>
#include <NTPClient.h>

static WiFiUDP ntpUDP;
// Offset 0: get raw UTC from NTP; cetOffset() applies the correct CET/CEST offset.
static NTPClient timeClient(ntpUDP, NTP_SERVER, 0);
static bool timeInitialized = false;

// Returns 7200 (CEST) or 3600 (CET) for a given UTC epoch time.
// Rule: CEST runs from last Sunday of March 01:00 UTC to last Sunday of October 01:00 UTC.
static int cetOffset(time_t utc)
{
    struct tm t;
    gmtime_r(&utc, &t);
    int y = t.tm_year + 1900;

    struct tm march31 = {0, 0, 1, 31, 2, y - 1900};
    mktime(&march31);
    int startDay = 31 - march31.tm_wday;

    struct tm oct31 = {0, 0, 1, 31, 9, y - 1900};
    mktime(&oct31);
    int endDay = 31 - oct31.tm_wday;

    struct tm dstStart = {0, 0, 1, startDay, 2, y - 1900};
    struct tm dstEnd   = {0, 0, 1, endDay,   9, y - 1900};
    time_t tStart = mktime(&dstStart);
    time_t tEnd   = mktime(&dstEnd);

    return (utc >= tStart && utc < tEnd) ? 7200 : 3600;
}
#endif

void initTimeSync()
{
#if defined(MATRIX_PORTAL_M4)
    timeClient.begin();
    // Set TZ so mktime() correctly converts Prague local timestamps from Golemio API to UTC
    setenv("TZ", TZ_POSIX, 1);
    tzset();
#else
    // configTzTime sets TZ + starts SNTP in one call; mktime() uses it for API timestamp parsing
    configTzTime(TZ_POSIX, NTP_SERVER);
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

    timeClient.update();

    time_t utc = timeClient.getEpochTime();
    time_t local = utc + cetOffset(utc);
    struct tm* timeinfo = gmtime(&local);

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
    time_t utc = timeClient.getEpochTime();
    time_t local = utc + cetOffset(utc);
    struct tm* t = gmtime(&local);

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
    return timeClient.getEpochTime(); // returns UTC; ETA calc compares against UTC departure times
#else
    time_t now;
    time(&now);
    return now;
#endif
}
