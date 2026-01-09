#include "GolemioAPI.h"
#include "../utils/Logger.h"
#include "../utils/gfxlatin2.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>

GolemioAPI::GolemioAPI()
{
}

GolemioAPI::APIResult GolemioAPI::fetchDepartures(const Config &config)
{
    APIResult result = {};
    result.departureCount = 0;
    result.hasError = false;
    result.stopName[0] = '\0';
    result.errorMsg[0] = '\0';

    // Validate inputs
    if (strlen(config.apiKey) == 0 || strlen(config.stopIds) == 0)
    {
        result.hasError = true;
        strlcpy(result.errorMsg, "Missing API key or stop IDs", sizeof(result.errorMsg));
        return result;
    }

    logTimestamp();
    Serial.println("API: Fetching departures...");
    logMemory("api_start");

    // Temporary array to collect all departures from all stops
    Departure tempDepartures[MAX_TEMP_DEPARTURES];
    int tempCount = 0;

    // Parse comma-separated stop IDs
    char stopIdsCopy[128];
    strlcpy(stopIdsCopy, config.stopIds, sizeof(stopIdsCopy));

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

        delay(1000);

        stopId = strtok(NULL, ",");
    }

    // Sort all collected departures by ETA
    if (tempCount > 0)
    {
        qsort(tempDepartures, tempCount, sizeof(Departure), compareDepartures);

        logTimestamp();
        Serial.print("Collected ");
        Serial.print(tempCount);
        Serial.println(" departures from all stops");
    }

    // Filter by minimum departure time and copy to final array
    result.departureCount = 0;
    for (int i = 0; i < tempCount && result.departureCount < MAX_DEPARTURES; i++)
    {
        if (tempDepartures[i].eta >= config.minDepartureTime)
        {
            result.departures[result.departureCount] = tempDepartures[i];
            result.departureCount++;
        }
    }

    logTimestamp();
    Serial.print("Final departures after filtering: ");
    Serial.println(result.departureCount);

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
                                 char *stopName, bool &isFirstStop)
{
    logTimestamp();
    Serial.print("API: Querying stop ");
    Serial.println(stopId);

    HTTPClient http;
    char url[512];

    // Query each stop individually with higher total to get more results
    snprintf(url, sizeof(url),
             "https://api.golemio.cz/v2/pid/departureboards?ids=%s&total=%d&preferredTimezone=Europe/Prague&minutesBefore=0&minutesAfter=120",
             stopId,
             config.numDepartures > MAX_DEPARTURES ? MAX_DEPARTURES : config.numDepartures);

    http.begin(url);
    http.addHeader("x-access-token", config.apiKey);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(HTTP_TIMEOUT_MS);

    int httpCode = http.GET();

    if (httpCode == HTTP_CODE_OK)
    {
        String payload = http.getString();
        DynamicJsonDocument doc(JSON_BUFFER_SIZE);
        DeserializationError error = deserializeJson(doc, payload);

        if (error)
        {
            logTimestamp();
            Serial.print("JSON Parse Error for stop ");
            Serial.print(stopId);
            Serial.print(": ");
            Serial.println(error.c_str());
            http.end();
            return false;
        }

        // Get stop name from first stop (for display)
        if (isFirstStop && doc.containsKey("stops") && doc["stops"].size() > 0)
        {
            const char *name = doc["stops"][0]["stop_name"];
            if (name)
            {
                strlcpy(stopName, name, 64);
                utf8tocp(stopName);
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

                parseDepartureObject(dep, tempDepartures, tempCount);
            }
        }

        http.end();
        return true;
    }
    else
    {
        logTimestamp();
        Serial.print("API Error for stop ");
        Serial.print(stopId);
        Serial.print(": HTTP ");
        Serial.println(httpCode);
        http.end();
        return false;
    }
}

void GolemioAPI::parseDepartureObject(JsonObject depJson, Departure *tempDepartures, int &tempCount)
{
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
        utf8tocp(tempDepartures[tempCount].destination);           // Then convert to ISO-8859-2
    }
    else
    {
        tempDepartures[tempCount].destination[0] = '\0';
    }

    // Calculate ETA from departure timestamp
    const char *timestamp = depJson["departure_timestamp"]["predicted"];
    if (!timestamp)
    {
        timestamp = depJson["departure_timestamp"]["scheduled"];
    }

    if (timestamp)
    {
        struct tm tm;
        strptime(timestamp, "%Y-%m-%dT%H:%M:%S", &tm);
        time_t depTime = mktime(&tm);
        time_t now;
        time(&now);
        int diffSec = difftime(depTime, now);
        tempDepartures[tempCount].eta = (diffSec > 0) ? (diffSec / 60) : 0;
    }
    else
    {
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
