#include "GolemioAPI.h"
#include "../utils/Logger.h"
#include <ArduinoJson.h>
#include <time.h>

// Simple ISO8601 timestamp parser (strptime replacement for platforms without it)
// Parses format: "YYYY-MM-DDTHH:MM:SS"
static bool parseISO8601(const char* timestamp, struct tm* tm)
{
    if (timestamp == nullptr || tm == nullptr) return false;

    memset(tm, 0, sizeof(struct tm));

    // Parse: YYYY-MM-DDTHH:MM:SS
    int year, month, day, hour, minute, second;
    if (sscanf(timestamp, "%d-%d-%dT%d:%d:%d", &year, &month, &day, &hour, &minute, &second) != 6)
    {
        return false;
    }

    tm->tm_year = year - 1900;  // Years since 1900
    tm->tm_mon = month - 1;     // Months since January (0-11)
    tm->tm_mday = day;
    tm->tm_hour = hour;
    tm->tm_min = minute;
    tm->tm_sec = second;
    tm->tm_isdst = -1;          // Let mktime determine DST

    return true;
}

// Platform-specific HTTP client includes
#if defined(MATRIX_PORTAL_M4)
    #include <WiFiNINA.h>
    #include <ArduinoHttpClient.h>
#else
    #include <HTTPClient.h>
#endif

GolemioAPI::GolemioAPI() : statusCallback(nullptr), partialResultsCallback(nullptr)
{
}

void GolemioAPI::setStatusCallback(APIStatusCallback callback)
{
    statusCallback = callback;
}

void GolemioAPI::setPartialResultsCallback(APIPartialResultsCallback callback)
{
    partialResultsCallback = callback;
}

TransitAPI::APIResult GolemioAPI::fetchDepartures(const Config &config)
{
    TransitAPI::APIResult result = {};
    result.departureCount = 0;
    result.hasError = false;
    result.stopName[0] = '\0';
    result.errorMsg[0] = '\0';

    // Validate inputs (use Prague-specific fields)
    if (strlen(config.pragueApiKey) == 0 || strlen(config.pragueStopIds) == 0)
    {
        result.hasError = true;
        strlcpy(result.errorMsg, "Missing API key or stop IDs", sizeof(result.errorMsg));
        return result;
    }

    logTimestamp();
    debugPrintln("API: Fetching departures...");
    logMemory("api_start");

    // Temporary array to collect all departures from all stops
    // IMPORTANT: Static to avoid stack overflow (~2KB array)
    static Departure tempDepartures[MAX_TEMP_DEPARTURES];
    int tempCount = 0;

    // Parse comma-separated stop IDs (use Prague-specific field)
    char stopIdsCopy[128];
    strlcpy(stopIdsCopy, config.pragueStopIds, sizeof(stopIdsCopy));

    char *stopId = strtok(stopIdsCopy, ",");
    bool firstStop = true;
    int stopIndex = 0;

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

        int countBefore = tempCount;
        querySingleStop(stopId, config, tempDepartures, tempCount, result.stopName, firstStop, stopIndex);

        // If we got new departures and have a callback, send partial results immediately
        if (tempCount > countBefore && partialResultsCallback)
        {
            // Sort current results
            qsort(tempDepartures, tempCount, sizeof(Departure), compareDepartures);

            // Copy top departures to result for callback (apply minDepartureTime filter)
            result.departureCount = 0;
            for (int i = 0; i < tempCount && result.departureCount < MAX_DEPARTURES; i++)
            {
                if (tempDepartures[i].eta > config.minDepartureTime)
                {
                    result.departures[result.departureCount] = tempDepartures[i];
                    result.departureCount++;
                }
            }

            logTimestamp();
            char msg[64];
            snprintf(msg, sizeof(msg), "Partial results: %d departures, triggering display", result.departureCount);
            debugPrintln(msg);

            partialResultsCallback(result.departures, result.departureCount, result.stopName);
        }

        delay(1000);

        stopId = strtok(NULL, ",");
        stopIndex++;
    }

    // Sort all collected departures by ETA
    if (tempCount > 0)
    {
        qsort(tempDepartures, tempCount, sizeof(Departure), compareDepartures);

        char msg[64];
        snprintf(msg, sizeof(msg), "Collected %d departures from all stops", tempCount);
        logTimestamp();
        debugPrintln(msg);
    }

    // Filter by minimum departure time and copy to final array
    result.departureCount = 0;
    for (int i = 0; i < tempCount && result.departureCount < MAX_DEPARTURES; i++)
    {
        if (tempDepartures[i].eta > config.minDepartureTime)
        {
            result.departures[result.departureCount] = tempDepartures[i];
            result.departureCount++;
        }
    }

    char filterMsg[64];
    snprintf(filterMsg, sizeof(filterMsg), "Final departures after filtering: %d", result.departureCount);
    logTimestamp();
    debugPrintln(filterMsg);

    // Set error status if no departures found
    if (tempCount == 0)
    {
        result.hasError = true;
        strlcpy(result.errorMsg, "No departures", sizeof(result.errorMsg));
    }

    logMemory("api_complete");

    return result;
}

bool GolemioAPI::querySingleStop(const char *stopId, const Config &config,
                                 Departure *tempDepartures, int &tempCount,
                                 char *stopName, bool &isFirstStop, int stopIndex)
{
    char queryMsg[96];
    snprintf(queryMsg, sizeof(queryMsg), "API: Querying stop %s", stopId);
    logTimestamp();
    debugPrintln(queryMsg);

    // Build the path for the API request
    char path[256];
    snprintf(path, sizeof(path),
             "/v2/pid/departureboards?ids=%s&total=%d&preferredTimezone=Europe/Prague&minutesBefore=%d&minutesAfter=120",
             stopId,
             MAX_DEPARTURES,
             config.minDepartureTime > 0 ? config.minDepartureTime * -1 : 0);

    const int MAX_RETRIES = 3;
    int httpCode = -1;
    String payload;

#if defined(MATRIX_PORTAL_M4)
    // M4 uses ArduinoHttpClient with WiFiSSLClient
    WiFiSSLClient sslClient;
    HttpClient http(sslClient, "api.golemio.cz", 443);
    http.setTimeout(HTTP_TIMEOUT_MS);

    logTimestamp();
    debugPrintln("API: Starting HTTP request (M4)...");

    for (int retry = 0; retry < MAX_RETRIES; retry++)
    {
        if (retry > 0)
        {
            int delayMs = 2000 * retry;
            char retryMsg[64];
            snprintf(retryMsg, sizeof(retryMsg), "API: Retry %d/%d after %dms", retry + 1, MAX_RETRIES, delayMs);
            logTimestamp();
            debugPrintln(retryMsg);
            delay(delayMs);
        }

        logTimestamp();
        debugPrintln("API: Sending request...");

        http.beginRequest();
        http.get(path);
        http.sendHeader("x-access-token", config.pragueApiKey);
        http.sendHeader("Content-Type", "application/json");
        http.endRequest();

        logTimestamp();
        debugPrintln("API: Waiting for response...");

        httpCode = http.responseStatusCode();

        logTimestamp();
        char httpMsg[64];
        snprintf(httpMsg, sizeof(httpMsg), "API: HTTP response code: %d", httpCode);
        debugPrintln(httpMsg);

        if (httpCode == 200)
        {
            logTimestamp();
            debugPrintln("API: Reading response body...");

            // Get content length and pre-allocate (much faster than growing String)
            int contentLen = http.contentLength();
            if (contentLen > 0 && contentLen < JSON_BUFFER_SIZE)
            {
                payload.reserve(contentLen + 1);
            }

            // Read in chunks (much faster than responseBody()'s character-by-character)
            char buffer[512];
            while (http.available())
            {
                int bytesRead = http.read((uint8_t *)buffer, sizeof(buffer) - 1);
                if (bytesRead > 0)
                {
                    buffer[bytesRead] = '\0';
                    payload += buffer;
                }
            }

            logTimestamp();
            char bodyMsg[64];
            snprintf(bodyMsg, sizeof(bodyMsg), "API: Response body length: %d", payload.length());
            debugPrintln(bodyMsg);
            break;
        }

        // Log error with diagnostics
        logTimestamp();
        char errMsg[128];
        snprintf(errMsg, sizeof(errMsg), "API: HTTP %d attempt %d/%d", httpCode, retry + 1, MAX_RETRIES);
        debugPrintln(errMsg);
        logNetworkDiagnostics();

        if (httpCode >= 400 && httpCode < 500) break;  // Don't retry client errors
    }

#else
    // ESP32 uses native HTTPClient
    HTTPClient http;
    char url[512];
    snprintf(url, sizeof(url), "https://api.golemio.cz%s", path);
    http.setTimeout(HTTP_TIMEOUT_MS);

    // Retry logic: establish fresh connection for each attempt
    // IMPORTANT: http.end() must be called before retrying to avoid socket leaks
    // NOTE: Status callback NOT called during retries - errors shown in status bar
    //       via the apiError mechanism, keeping departures visible
    for (int retry = 0; retry < MAX_RETRIES; retry++)
    {
        if (retry > 0)
        {
            int delayMs = 2000 * retry; // 2s, 4s, 6s backoff

            char retryMsg[64];
            snprintf(retryMsg, sizeof(retryMsg), "API: Retry %d/%d after %dms", retry + 1, MAX_RETRIES, delayMs);
            logTimestamp();
            debugPrintln(retryMsg);
            delay(delayMs);
        }

        // Establish connection for this attempt
        http.begin(url);
        http.addHeader("x-access-token", config.pragueApiKey);
        http.addHeader("Content-Type", "application/json");

        httpCode = http.GET();

        // Success - break out of retry loop
        if (httpCode == HTTP_CODE_OK)
        {
            payload = http.getString();
            break;
        }

        // Close connection BEFORE logging or retrying to free socket immediately
        http.end();

        // Don't retry on 4xx errors (client errors - won't fix with retry)
        if (httpCode >= 400 && httpCode < 500)
        {
            char clientErrMsg[128];
            snprintf(clientErrMsg, sizeof(clientErrMsg), "API: Client error %d (%s) - no retry",
                     httpCode, httpErrorToString(httpCode));
            logTimestamp();
            debugPrintln(clientErrMsg);
            logNetworkDiagnostics();
            break;
        }

        // Log retry-able errors with diagnostics
        char errMsg[128];
        if (retry < MAX_RETRIES - 1)
        {
            snprintf(errMsg, sizeof(errMsg), "API: HTTP %d (%s) attempt %d/%d - will retry",
                     httpCode, httpErrorToString(httpCode), retry + 1, MAX_RETRIES);
        }
        else
        {
            snprintf(errMsg, sizeof(errMsg), "API: HTTP %d (%s) - all %d attempts failed",
                     httpCode, httpErrorToString(httpCode), MAX_RETRIES);
        }
        logTimestamp();
        debugPrintln(errMsg);
        logNetworkDiagnostics();
    }
#endif

#if defined(MATRIX_PORTAL_M4)
    if (httpCode == 200)
#else
    if (httpCode == HTTP_CODE_OK)
#endif
    {
        DynamicJsonDocument doc(JSON_BUFFER_SIZE);
        DeserializationError error = deserializeJson(doc, payload);

        if (error)
        {
            char jsonErrMsg[128];
            snprintf(jsonErrMsg, sizeof(jsonErrMsg), "JSON Parse Error for stop %s: %s", stopId, error.c_str());
            logTimestamp();
            debugPrintln(jsonErrMsg);
#if !defined(MATRIX_PORTAL_M4)
            http.end();
#endif
            return false;
        }

        // Get stop name from first stop (for display)
        if (isFirstStop && doc.containsKey("stops") && doc["stops"].size() > 0)
        {
            const char *name = doc["stops"][0]["stop_name"];
            if (name)
            {
                strlcpy(stopName, name, 64);
                // Note: UTF-8 to ISO-8859-2 conversion now handled by DisplayManager
            }
            isFirstStop = false;
        }

        // Parse departures from this stop
        if (doc.containsKey("departures"))
        {
            JsonArray deps = doc["departures"];

            for (JsonObject dep : deps)
            {
                if (tempCount >= MAX_TEMP_DEPARTURES)
                    break;

                parseDepartureObject(dep, tempDepartures, tempCount, stopIndex);
            }
        }

#if !defined(MATRIX_PORTAL_M4)
        http.end();
#endif
        return true;
    }
    else
    {
        char failMsg[128];
        snprintf(failMsg, sizeof(failMsg), "API: Failed after %d attempts for stop %s - HTTP %d",
                 MAX_RETRIES, stopId, httpCode);
        logTimestamp();
        debugPrintln(failMsg);
#if !defined(MATRIX_PORTAL_M4)
        http.end();
#endif
        return false;
    }
}

void GolemioAPI::parseDepartureObject(JsonObject depJson, Departure *tempDepartures, int &tempCount, int stopIndex)
{
    // Set stop index
    tempDepartures[tempCount].stopIndex = stopIndex;

    // Route/Line info
    const char *line = depJson["route"]["short_name"];
    if (line)
    {
        strlcpy(tempDepartures[tempCount].line, line, sizeof(tempDepartures[0].line));
    }
    else
    {
        tempDepartures[tempCount].line[0] = '\0';
    }

    // Destination/Headsign
    const char *headsign = depJson["trip"]["headsign"];
    if (headsign)
    {
        strlcpy(tempDepartures[tempCount].destination, headsign, sizeof(tempDepartures[0].destination));
        shortenDestination(tempDepartures[tempCount].destination); // Shorten while still UTF-8
        // Note: UTF-8 to ISO-8859-2 conversion now handled by DisplayManager
    }
    else
    {
        tempDepartures[tempCount].destination[0] = '\0';
    }

    // Parse and store departure timestamp
    const char *timestamp = depJson["departure_timestamp"]["predicted"];
    if (!timestamp)
    {
        timestamp = depJson["departure_timestamp"]["scheduled"];
    }

    if (timestamp)
    {
        struct tm tm;
        parseISO8601(timestamp, &tm);
        time_t depTime = mktime(&tm);

        // Store timestamp for future recalculation
        tempDepartures[tempCount].departureTime = depTime;

        // Calculate initial ETA for sorting/filtering
        tempDepartures[tempCount].eta = calculateETA(depTime);
    }
    else
    {
        tempDepartures[tempCount].departureTime = 0;
        tempDepartures[tempCount].eta = 0;
    }

    // Air conditioning
    tempDepartures[tempCount].hasAC = depJson["trip"]["is_air_conditioned"] | false;

    // Delay info
    if (depJson.containsKey("delay") && !depJson["delay"].isNull())
    {
        tempDepartures[tempCount].isDelayed = true;
        tempDepartures[tempCount].delayMinutes = depJson["delay"]["minutes"] | 0;
    }
    else
    {
        tempDepartures[tempCount].isDelayed = false;
        tempDepartures[tempCount].delayMinutes = 0;
    }

    tempCount++;
}
