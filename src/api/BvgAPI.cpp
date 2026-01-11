#include "BvgAPI.h"
#include "../utils/Logger.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <cstring>

BvgAPI::BvgAPI() : statusCallback(nullptr)
{
}

void BvgAPI::setStatusCallback(APIStatusCallback callback)
{
    statusCallback = callback;
}

TransitAPI::APIResult BvgAPI::fetchDepartures(const Config &config)
{
    TransitAPI::APIResult result = {};
    result.departureCount = 0;
    result.hasError = false;
    result.stopName[0] = '\0';
    result.errorMsg[0] = '\0';

    // Validate inputs (use Berlin-specific field)
    if (strlen(config.berlinStopIds) == 0)
    {
        result.hasError = true;
        strlcpy(result.errorMsg, "Missing stop IDs", sizeof(result.errorMsg));
        return result;
    }

    logTimestamp();
    debugPrintln("BVG API: Fetching departures...");
    logMemory("bvg_api_start");

    // Temporary array to collect all departures from all stops
    // IMPORTANT: Static to avoid stack overflow (~2KB array)
    static Departure tempDepartures[MAX_TEMP_DEPARTURES];
    int tempCount = 0;

    // Parse comma-separated stop IDs (use Berlin-specific field)
    char stopIdsCopy[128];
    strlcpy(stopIdsCopy, config.berlinStopIds, sizeof(stopIdsCopy));

    char *stopId = strtok(stopIdsCopy, ",");
    bool firstStop = true;

    while (stopId != NULL && tempCount < MAX_TEMP_DEPARTURES)
    {
        // Trim whitespace
        while (*stopId == ' ')
            stopId++;

        if (strlen(stopId) == 0)
        {
            stopId = strtok(NULL, ",");
            continue;
        }

        querySingleStop(stopId, config, tempDepartures, tempCount, result.stopName, firstStop);

        // Rate limiting: 1-second delay between API calls
        delay(1000);

        stopId = strtok(NULL, ",");
    }

    // Sort all collected departures by ETA (ascending)
    qsort(tempDepartures, tempCount, sizeof(Departure), compareDepartures);

    // Filter by minimum departure time and copy to final array (up to MAX_DEPARTURES for caching)
    // Note: config.numDepartures is used for display only, not for limiting cache size
    result.departureCount = 0;
    for (int i = 0; i < tempCount && result.departureCount < MAX_DEPARTURES; i++)
    {
        // Skip departures below minimum departure time
        if (tempDepartures[i].eta < config.minDepartureTime)
        {
            continue;
        }

        result.departures[result.departureCount] = tempDepartures[i];
        result.departureCount++;
    }

    if (result.departureCount == 0)
    {
        result.hasError = true;
        strlcpy(result.errorMsg, "No departures", sizeof(result.errorMsg));
    }

    logTimestamp();
    char msg[64];
    snprintf(msg, sizeof(msg), "BVG API: Fetched %d departures", result.departureCount);
    debugPrintln(msg);
    logMemory("bvg_api_end");

    return result;
}

bool BvgAPI::querySingleStop(const char *stopId, const Config &config,
                             Departure *tempDepartures, int &tempCount,
                             char *stopName, bool &isFirstStop)
{
    // Build URL with 'when' parameter to offset query time by minDepartureTime
    // Format: https://v6.bvg.transport.rest/stops/{stopId}/departures?duration=120&results=12&when={unix_timestamp}
    char url[256];

    // Calculate offset time: current time + minDepartureTime (in seconds)
    // BVG API accepts Unix timestamps (seconds since epoch)
    // Add 90-second buffer: BVG API returns departures ~80s before 'when' time + HTTP latency
    time_t now = time(nullptr);
    time_t whenTime = now + (config.minDepartureTime * 60) + 90;

    snprintf(url, sizeof(url),
             "https://v6.bvg.transport.rest/stops/%s/departures?duration=120&results=12&when=%ld",
             stopId, (long)whenTime);

    HTTPClient http;
    http.setTimeout(HTTP_TIMEOUT_MS);

    logTimestamp();
    debugPrint("BVG API: Querying stop ");
    debugPrintln(stopId);
    logTimestamp();
    char debugMsg[128];
    snprintf(debugMsg, sizeof(debugMsg), "BVG API: URL: %s (now=%ld, when=%ld, offset=%d min)",
             url, (long)now, (long)whenTime, config.minDepartureTime);
    debugPrintln(debugMsg);

    bool success = false;
    int httpCode = 0;

    // Retry logic: 3 attempts with exponential backoff
    for (int attempt = 1; attempt <= 3; attempt++)
    {
        http.begin(url);
        // BVG API requires no authentication
        http.addHeader("Content-Type", "application/json");

        httpCode = http.GET();

        if (httpCode == HTTP_CODE_OK)
        {
            success = true;
            break;
        }
        else
        {
            logTimestamp();
            char errMsg[64];
            snprintf(errMsg, sizeof(errMsg), "BVG API: HTTP error %d (attempt %d/3)", httpCode, attempt);
            debugPrintln(errMsg);

            // Don't retry on 4xx client errors
            if (httpCode >= 400 && httpCode < 500)
            {
                break;
            }

            // Exponential backoff for retries
            if (attempt < 3)
            {
                int delayMs = attempt * 2000; // 2s, 4s, 6s
                if (statusCallback)
                {
                    char statusMsg[32];
                    snprintf(statusMsg, sizeof(statusMsg), "API Retry %d/3", attempt);
                    statusCallback(statusMsg);
                }

                // Show error briefly before retry
                delay(1000);
                delay(delayMs);
            }
        }
    }

    if (!success)
    {
        http.end();
        logTimestamp();
        debugPrintln("BVG API: Failed after retries");
        return false;
    }

    // Parse JSON response
    String payload = http.getString();
    http.end();

    DynamicJsonDocument doc(JSON_BUFFER_SIZE);
    DeserializationError error = deserializeJson(doc, payload);

    if (error)
    {
        logTimestamp();
        debugPrint("BVG API: JSON parse error: ");
        debugPrintln(error.c_str());
        return false;
    }

    // BVG API returns: {"departures": [...]}
    JsonArray departures = doc["departures"].as<JsonArray>();

    if (departures.isNull())
    {
        logTimestamp();
        debugPrintln("BVG API: No departures array in response");
        return false;
    }

    logTimestamp();
    char countMsg[64];
    snprintf(countMsg, sizeof(countMsg), "BVG API: Found %d departures in JSON", departures.size());
    debugPrintln(countMsg);

    // Extract stop name from first departure if this is the first stop
    if (isFirstStop && departures.size() > 0)
    {
        JsonObject firstDep = departures[0];
        if (firstDep.containsKey("stop") && firstDep["stop"].containsKey("name"))
        {
            const char *name = firstDep["stop"]["name"];
            if (name)
            {
                strlcpy(stopName, name, 64);
            }
        }
        isFirstStop = false;
    }

    // Parse each departure
    int beforeParse = tempCount;
    for (JsonObject depJson : departures)
    {
        if (tempCount >= MAX_TEMP_DEPARTURES)
            break;

        parseDepartureObject(depJson, tempDepartures, tempCount);
    }

    logTimestamp();
    int parsedCount = tempCount - beforeParse;
    snprintf(countMsg, sizeof(countMsg), "BVG API: Parsed %d departures (total now: %d)", parsedCount, tempCount);
    debugPrintln(countMsg);

    return true;
}

void BvgAPI::parseDepartureObject(JsonObject depJson, Departure *tempDepartures, int &tempCount)
{
    // Extract line name (from line.name)
    if (!depJson.containsKey("line") || !depJson["line"].containsKey("name"))
    {
        logTimestamp();
        debugPrintln("BVG API: Skipping departure - no line info");
        return; // Skip if no line info
    }

    const char *lineName = depJson["line"]["name"];
    if (!lineName || strlen(lineName) == 0)
    {
        logTimestamp();
        debugPrintln("BVG API: Skipping departure - empty line name");
        return;
    }

    strlcpy(tempDepartures[tempCount].line, lineName, sizeof(tempDepartures[tempCount].line));

    // Extract direction (destination)
    const char *direction = depJson["direction"].as<const char*>();
    if (!direction || strlen(direction) == 0)
    {
        logTimestamp();
        debugPrintln("BVG API: Skipping departure - no direction");
        return; // Skip if no destination
    }

    // Extract platform and append to destination if present
    // const char *platform = depJson["platform"].as<const char*>();
    // if (platform && strlen(platform) > 0)
    // {
    //     // Parse platform: if it contains a space (e.g., "Gleis 12"), use only the part after the space
    //     // Otherwise use the entire string (e.g., "12", "A", "1a")
    //     const char *platformDisplay = platform;
    //     const char *spacePos = strchr(platform, ' ');
    //     if (spacePos != nullptr)
    //     {
    //         platformDisplay = spacePos + 1;  // Use part after space
    //     }

    //     // Format: "Destination <Pl>"
    //     snprintf(tempDepartures[tempCount].destination, sizeof(tempDepartures[tempCount].destination),
    //              "%s %s", direction, platformDisplay);
    // }
    // else
    // {
        strlcpy(tempDepartures[tempCount].destination, direction, sizeof(tempDepartures[tempCount].destination));
    // }

    // Parse ISO 8601 timestamp from "when" field
    const char *when = depJson["when"].as<const char*>();
    if (!when || strlen(when) == 0)
    {
        logTimestamp();
        debugPrintln("BVG API: Skipping departure - no timestamp");
        return; // Skip if no timestamp
    }

    // Parse: "2026-01-11T14:30:00+01:00"
    struct tm tm;
    memset(&tm, 0, sizeof(tm));

    // Note: strptime doesn't handle timezone offset well on ESP32, so we parse basic format
    // and ignore timezone (Berlin is always CET/CEST which matches Prague)
    if (strptime(when, "%Y-%m-%dT%H:%M:%S", &tm) == NULL)
    {
        logTimestamp();
        debugPrint("BVG API: Skipping departure - failed to parse timestamp: ");
        debugPrintln(when);
        return; // Skip if timestamp parse fails
    }

    // Tell mktime to auto-determine DST
    tm.tm_isdst = -1;

    tempDepartures[tempCount].departureTime = mktime(&tm);

    // Calculate ETA (minutes from now)
    time_t now = time(nullptr);
    int etaSeconds = tempDepartures[tempCount].departureTime - now;

    // Skip departures that are in the past (negative or zero etaSeconds)
    // Must check BEFORE division because -4/60 = 0 (integer division rounds toward zero)
    if (etaSeconds < 0)
    {
        logTimestamp();
        char debugMsg[64];
        snprintf(debugMsg, sizeof(debugMsg), "BVG API: Skipping departure - in the past: %d seconds", etaSeconds);
        debugPrintln(debugMsg);
        return;
    }

    tempDepartures[tempCount].eta = etaSeconds / 60;

    // Debug log for first few departures
    if (tempCount < 16)
    {
        logTimestamp();
        char debugMsg[128];
        snprintf(debugMsg, sizeof(debugMsg), "BVG API: Line %s to %s - ETA: %d min (when: %s, now: %ld, dep: %ld)",
                 lineName, direction, tempDepartures[tempCount].eta, when, (long)now, (long)tempDepartures[tempCount].departureTime);
        debugPrintln(debugMsg);
    }

    // Parse delay (seconds in BVG API)
    if (!depJson["delay"].isNull())
    {
        int delaySec = depJson["delay"] | 0;
        tempDepartures[tempCount].delayMinutes = delaySec / 60;
        tempDepartures[tempCount].isDelayed = (delaySec >= 60);  // Only flag if ≥1 minute
    }
    else
    {
        tempDepartures[tempCount].delayMinutes = 0;
        tempDepartures[tempCount].isDelayed = false;
    }

    // BVG API doesn't provide AC info
    tempDepartures[tempCount].hasAC = false;

    tempCount++;
}
